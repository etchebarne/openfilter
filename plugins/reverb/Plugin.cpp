#include "Editor.hpp"
#include "Engine.hpp"
#include <atomic>
#include <bit>
#include <charconv>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <new>
#include <openfilter/plugin/SpscQueue.hpp>
#include <openfilter/plugin/Stream.hpp>
#include <openfilter/plugin/TripleBuffer.hpp>
#include <string_view>

namespace openfilter::reverb {
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
const char *const features[]{CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB,
                             CLAP_PLUGIN_FEATURE_STEREO, CLAP_PLUGIN_FEATURE_MONO, nullptr};
const clap_plugin_descriptor descriptor{CLAP_VERSION,
                                        "org.openfilter.reverb",
                                        "OpenFilter Reverb",
                                        "OpenFilter",
                                        "",
                                        "",
                                        "",
                                        "0.2.0",
                                        "Algorithmic space with decay shaping and post EQ",
                                        features};

using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Ignore,
                                   clap::helpers::CheckingLevel::None>;
class Plugin final : public Base {
  public:
    explicit Plugin(const clap_host *host) : Base(&descriptor, host), host_(host) {
        publish();
        engine_.prepare(48000, base_);
    }
    ~Plugin() override { guiDestroy(); }

  private:
    struct Snapshot {
        Values values{};
        unsigned revision = 2;
        uint64_t serial = 0;
        Values effective{};
        uint64_t uiSerial = 0;
        std::array<double, 4> peaks{};
        bool clipped = false;
        double reduction = 0;
    };
    const clap_host *host_;
    Engine engine_;
    unsigned revision_ = 2;
    Values base_ = defaults(), modulation_{};
    plugin::TripleBuffer<Snapshot> published_;
    uint64_t audioSerial_ = 0; // audio-thread owned while active
    plugin::SpscQueue<Snapshot, 8> pending_;
    Snapshot requested_{}; // main-thread only
    uint32_t channels_ = 2, maxFrames_ = 0;
    double rate_ = 48000, meterDecay_ = 1;
    std::array<double, 4> peaks_{};
    bool clipped_ = false;
    ui::AnalysisTap analysis_;
    plugin::SpscQueue<UiMessage, 4096> uiQueue_;
    std::deque<UiMessage> uiWaiting_; // main thread; audio never touches this container
    uint64_t uiSerial_ = 0, audioUiSerial_ = 0;
    Values uiDesired_ = defaults();
    std::array<uint64_t, parameterCount> uiDesiredSerial_{};
    std::unique_ptr<Editor> editor_;
    clap_id timer_ = CLAP_INVALID_ID;

    void sendUi(UiKind kind, unsigned i, double value) {
        UiMessage msg{kind, i, value, ++uiSerial_, requested_.serial};
        if (kind == UiKind::Value) {
            uiDesired_[i] = value;
            uiDesiredSerial_[i] = msg.serial;
        }
        uiWaiting_.push_back(msg);
        pumpUi();
    }
    void pumpUi() {
        while (!uiWaiting_.empty() && uiQueue_.push(uiWaiting_.front()))
            uiWaiting_.pop_front();
        if (published_.read().uiSerial == uiSerial_)
            return;
        const auto *params =
            static_cast<const clap_host_params *>(host_->get_extension(host_, CLAP_EXT_PARAMS));
        if (params && params->request_flush)
            params->request_flush(host_);
        else
            host_->request_process(host_);
    }
    void consumeUi(const clap_output_events *out) noexcept {
        UiMessage msg;
        for (unsigned n = 0; n < 1024 && uiQueue_.peek(msg); ++n) {
            if (msg.kind == UiKind::ClearClip)
                clipped_ = false;
            else if (msg.kind == UiKind::Value &&
                     msg.stateSerial != audioSerial_) { /* superseded by a state load */
            } else {
                clap_event_param_value value{};
                clap_event_param_gesture gesture{};
                const clap_event_header *header = nullptr;
                if (msg.kind == UiKind::Value) {
                    value.header = {sizeof(value), 0, CLAP_CORE_EVENT_SPACE_ID,
                                    CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_IS_LIVE};
                    value.param_id = parameter(msg.index).id;
                    value.value = msg.value;
                    value.note_id = value.port_index = value.channel = value.key = -1;
                    header = &value.header;
                } else {
                    gesture.header = {sizeof(gesture), 0, CLAP_CORE_EVENT_SPACE_ID,
                                      static_cast<uint16_t>(msg.kind == UiKind::Begin
                                                                ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                                : CLAP_EVENT_PARAM_GESTURE_END),
                                      CLAP_EVENT_IS_LIVE};
                    gesture.param_id = parameter(msg.index).id;
                    header = &gesture.header;
                }
                // Retain the command, including gesture ends, if the host is full.
                if (!out || !out->try_push || !out->try_push(out, header))
                    break;
                if (msg.kind == UiKind::Value) {
                    base_[msg.index] = parameter(msg.index).constrain(msg.value);
                    engine_.set(msg.index, parameter(msg.index).constrain(base_[msg.index] +
                                                                          modulation_[msg.index]));
                }
            }
            uiQueue_.pop(msg);
            audioUiSerial_ = msg.serial;
        }
    }
    EditorState editorState() {
        const auto latest = published_.read();
        EditorState state;
        state.values = requested_.serial > latest.serial ? requested_.values : latest.values;
        state.effective = requested_.serial > latest.serial ? requested_.values : latest.effective;
        for (unsigned i = 0; i < parameterCount; ++i)
            if (uiDesiredSerial_[i] > latest.uiSerial) {
                state.effective[i] =
                    parameter(i).constrain(state.effective[i] + uiDesired_[i] - state.values[i]);
                state.values[i] = uiDesired_[i];
            }
        state.peaks = latest.peaks;
        state.clipped = latest.clipped;
        state.reduction = latest.reduction;
        state.rate = rate_;
        state.mono = channels_ == 1;
        state.stateSerial = requested_.serial;
        state.revision = requested_.serial > latest.serial ? requested_.revision : latest.revision;
        return state;
    }

    bool implementsGui() const noexcept override { return true; }
    bool guiIsApiSupported(const char *api, bool floating) noexcept override {
        return api && !std::strcmp(api, CLAP_WINDOW_API_X11) && !floating;
    }
    bool guiGetPreferredApi(const char **api, bool *floating) noexcept override {
        *api = CLAP_WINDOW_API_X11;
        *floating = false;
        return true;
    }
    bool guiCreate(const char *api, bool floating) noexcept override {
        if (editor_ || !guiIsApiSupported(api, floating))
            return false;
        const auto *timers = static_cast<const clap_host_timer_support *>(
            host_->get_extension(host_, CLAP_EXT_TIMER_SUPPORT));
        if (!timers || !timers->register_timer)
            return false;
        try {
            editor_ = std::make_unique<Editor>(
                [this] { return editorState(); },
                [this](UiKind k, unsigned i, double v) { sendUi(k, i, v); }, analysis_);
        } catch (...) {
            return false;
        }
        if (!timers->register_timer(host_, 33, &timer_)) {
            editor_.reset();
            return false;
        }
        return true;
    }
    void guiDestroy() noexcept override {
        editor_.reset();
        if (timer_ != CLAP_INVALID_ID) {
            const auto *timers = static_cast<const clap_host_timer_support *>(
                host_->get_extension(host_, CLAP_EXT_TIMER_SUPPORT));
            if (timers && timers->unregister_timer)
                timers->unregister_timer(host_, timer_);
            timer_ = CLAP_INVALID_ID;
        }
    }
    bool guiSetParent(const clap_window *window) noexcept override {
        return editor_ && window && window->api && !std::strcmp(window->api, CLAP_WINDOW_API_X11) &&
               editor_->attach(window->x11);
    }
    bool guiShow() noexcept override { return editor_ && editor_->show(); }
    bool guiHide() noexcept override {
        if (!editor_)
            return false;
        editor_->hide();
        return true;
    }
    bool guiSetScale(double scale) noexcept override { return editor_ && editor_->setScale(scale); }
    bool guiGetSize(uint32_t *w, uint32_t *h) noexcept override {
        if (!editor_ || !w || !h)
            return false;
        *w = editor_->width();
        *h = editor_->height();
        return true;
    }
    bool guiCanResize() const noexcept override { return true; }
    bool guiGetResizeHints(clap_gui_resize_hints *hints) noexcept override {
        if (!hints)
            return false;
        *hints = {true, true, false, 0, 0};
        return true;
    }
    bool guiAdjustSize(uint32_t *w, uint32_t *h) noexcept override {
        if (!editor_ || !w || !h)
            return false;
        const double s = editor_->scale();
        *w = std::clamp(*w, static_cast<uint32_t>(900 * s), static_cast<uint32_t>(2400 * s));
        *h = std::clamp(*h, static_cast<uint32_t>(600 * s), static_cast<uint32_t>(1400 * s));
        return true;
    }
    bool guiSetSize(uint32_t w, uint32_t h) noexcept override {
        return editor_ && editor_->resize(w, h);
    }
    bool implementsTimerSupport() const noexcept override { return true; }
    void onTimer(clap_id id) noexcept override {
        if (id != timer_)
            return;
        try {
            pumpUi();
            if (editor_)
                editor_->tick();
        } catch (...) { /* never unwind through the host ABI */
        }
    }

    void publish() noexcept {
        Snapshot s;
        s.values = base_;
        s.revision = revision_;
        s.serial = audioSerial_;
        s.uiSerial = audioUiSerial_;
        s.peaks = peaks_;
        s.clipped = clipped_;
        s.reduction = engine_.gainReduction();
        for (unsigned i = 0; i < parameterCount; ++i)
            s.effective[i] = parameter(i).constrain(base_[i] + modulation_[i]);
        published_.publish(s);
    }
    bool snapshot(Values &values, unsigned *revision = nullptr) noexcept {
        const auto &latest = published_.read();
        values = requested_.serial > latest.serial ? requested_.values : latest.values;
        if (revision)
            *revision = requested_.serial > latest.serial ? requested_.revision : latest.revision;
        for (unsigned i = 0; i < parameterCount; ++i)
            if (uiDesiredSerial_[i] > latest.uiSerial)
                values[i] = uiDesired_[i];
        return true;
    }
    void consumeState() noexcept {
        Snapshot s;
        bool changed = false;
        uint64_t serial = 0;
        // Bound work even if a producer is continuously loading presets.
        for (unsigned n = 0; n < 7 && pending_.pop(s); ++n) {
            base_ = s.values;
            revision_ = s.revision;
            serial = s.serial;
            changed = true;
        }
        if (!changed)
            return;
        modulation_.fill(0);
        engine_.setRevision(revision_, base_);
        for (unsigned i = 0; i < parameterCount; ++i)
            engine_.set(i, base_[i]);
        audioSerial_ = serial;
        publish();
    }
    void event(const clap_event_header *h) noexcept {
        if (!h || h->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;
        if (h->type == CLAP_EVENT_TRANSPORT && h->size >= sizeof(clap_event_transport)) {
            const auto &t = *reinterpret_cast<const clap_event_transport *>(h);
            if (t.flags & CLAP_TRANSPORT_HAS_TEMPO)
                engine_.tempo(t.tempo);
        }
        if (h->type == CLAP_EVENT_PARAM_VALUE && h->size >= sizeof(clap_event_param_value)) {
            const auto &e = *reinterpret_cast<const clap_event_param_value *>(h);
            const int i = indexForId(e.param_id);
            if (i < 0 || !std::isfinite(e.value) || e.note_id != -1 || e.port_index != -1 ||
                e.channel != -1 || e.key != -1)
                return;
            base_[i] = parameter(i).constrain(e.value);
            engine_.set(i, parameter(i).constrain(base_[i] + modulation_[i]));
        } else if (h->type == CLAP_EVENT_PARAM_MOD && h->size >= sizeof(clap_event_param_mod)) {
            const auto &e = *reinterpret_cast<const clap_event_param_mod *>(h);
            const int i = indexForId(e.param_id);
            if (i < 0 || !std::isfinite(e.amount) || !parameter(i).modulatable || e.note_id != -1 ||
                e.port_index != -1 || e.channel != -1 || e.key != -1)
                return;
            modulation_[i] = e.amount;
            engine_.set(i, parameter(i).constrain(base_[i] + modulation_[i]));
        }
    }
    bool activate(double rate, uint32_t minFrames, uint32_t maxFrames) noexcept override {
        if (!std::isfinite(rate) || rate < 1000 || rate > 768000 || minFrames == 0 ||
            maxFrames < minFrames)
            return false;
        consumeState();
        maxFrames_ = maxFrames;
        modulation_.fill(0);
        engine_.prepare(rate, base_, revision_);
        rate_ = rate;
        meterDecay_ = std::exp(-1 / (rate * .4));
        peaks_.fill(0);
        clipped_ = false;
        analysis_.reset(rate);
        return true;
    }
    void deactivate() noexcept override { consumeState(); }
    void reset() noexcept override {
        consumeState();
        modulation_.fill(0);
        engine_.reset(base_);
        peaks_.fill(0);
        clipped_ = false;
        analysis_.reset(rate_);
    }
    template <class T> void render(const clap_process *p, T **in, T **out) noexcept {
        const auto *events = p->in_events;
        const uint32_t count = events ? events->size(events) : 0;
        uint32_t next = 0;
        const bool analyze = analysis_.enabled.load(std::memory_order_relaxed);
        for (uint32_t frame = 0; frame < p->frames_count; ++frame) {
            while (next < count) {
                const auto *e = events->get(events, next);
                if (!e) {
                    ++next;
                    continue;
                }
                if (e->time > frame)
                    break;
                event(e);
                ++next;
            }
            auto normal = [](T sample) {
                return std::fpclassify(sample) == FP_SUBNORMAL ? T(0) : sample;
            };
            double l = normal(in[0][frame]), r = channels_ == 2 ? normal(in[1][frame]) : 0;
            const double il = l, ir = channels_ == 2 ? r : l;
            engine_.sample(l, r, channels_ == 1);
            out[0][frame] = normal(static_cast<T>(l));
            if (channels_ == 2)
                out[1][frame] = normal(static_cast<T>(r));
            const double values[]{il, ir, l, channels_ == 2 ? r : l};
            for (unsigned c = 0; c < 4; ++c)
                peaks_[c] = std::max(std::isfinite(values[c]) ? std::abs(values[c]) : 0.,
                                     peaks_[c] * meterDecay_);
            clipped_ = clipped_ || std::abs(l) > 1 || std::abs(r) > 1;
            if (analyze)
                analysis_.sample(engine_.wet(0), engine_.wet(1), l, channels_ == 2 ? r : l);
        }
    }
    clap_process_status process(const clap_process *p) noexcept override {
        if (!p || p->frames_count > maxFrames_ || p->audio_inputs_count != 1 ||
            p->audio_outputs_count != 1 || !p->audio_inputs || !p->audio_outputs)
            return CLAP_PROCESS_ERROR;
        const auto &in = p->audio_inputs[0];
        auto &out = p->audio_outputs[0];
        if (in.channel_count != channels_ || out.channel_count != channels_)
            return CLAP_PROCESS_ERROR;
        consumeState();
        consumeUi(p->out_events);
        if (p->transport && (p->transport->flags & CLAP_TRANSPORT_HAS_TEMPO))
            engine_.tempo(p->transport->tempo);
        if (in.data32 && out.data32) {
            for (unsigned c = 0; c < channels_; ++c)
                if (!in.data32[c] || !out.data32[c])
                    return CLAP_PROCESS_ERROR;
            render(p, in.data32, out.data32);
        } else if (in.data64 && out.data64) {
            for (unsigned c = 0; c < channels_; ++c)
                if (!in.data64[c] || !out.data64[c])
                    return CLAP_PROCESS_ERROR;
            render(p, in.data64, out.data64);
        } else
            return CLAP_PROCESS_ERROR;
        out.constant_mask = 0;
        publish();
        return CLAP_PROCESS_CONTINUE;
    }
    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool) const noexcept override { return 1; }
    bool audioPortsInfo(uint32_t index, bool input,
                        clap_audio_port_info *info) const noexcept override {
        if (index >= 1 || !info)
            return false;
        *info = {};
        info->id = input ? 0 : 1;
        std::snprintf(info->name, sizeof(info->name), "%s", input ? "Input" : "Output");
        info->flags = (index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0) | CLAP_AUDIO_PORT_SUPPORTS_64BITS |
                      CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
        info->channel_count = channels_;
        info->port_type = channels_ == 2 ? CLAP_PORT_STEREO : CLAP_PORT_MONO;
        info->in_place_pair = index == 0 ? (input ? 1 : 0) : CLAP_INVALID_ID;
        return true;
    }
    bool implementsAudioPortsConfig() const noexcept override { return true; }
    uint32_t audioPortsConfigCount() const noexcept override { return 2; }
    bool audioPortsGetConfig(uint32_t i, clap_audio_ports_config *c) const noexcept override {
        if (i > 1 || !c)
            return false;
        *c = {};
        c->id = i;
        const auto *type = i == 0 ? CLAP_PORT_STEREO : CLAP_PORT_MONO;
        std::snprintf(c->name, sizeof(c->name), "%s", i == 0 ? "Stereo" : "Mono");
        c->input_port_count = 1;
        c->output_port_count = 1;
        c->has_main_input = c->has_main_output = true;
        c->main_input_channel_count = c->main_output_channel_count = i == 0 ? 2 : 1;
        c->main_input_port_type = c->main_output_port_type = type;
        return true;
    }
    bool audioPortsSetConfig(clap_id id) noexcept override {
        if (isActive() || id > 1)
            return false;
        channels_ = id == 0 ? 2 : 1;
        return true;
    }
    bool implementsLatency() const noexcept override { return true; }
    uint32_t latencyGet() const noexcept override { return engine_.latency(); }
    bool implementsTail() const noexcept override { return true; }
    uint32_t tailGet() const noexcept override { return UINT32_MAX; }
    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return parameterCount; }
    bool paramsInfo(uint32_t i, clap_param_info *info) const noexcept override {
        if (i >= parameterCount || !info)
            return false;
        const auto p = parameter(i);
        *info = {};
        info->id = p.id;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
        if (p.stepped)
            info->flags |= CLAP_PARAM_IS_STEPPED;
        if (p.modulatable)
            info->flags |= CLAP_PARAM_IS_MODULATABLE;
        if (i == 0)
            info->flags |= CLAP_PARAM_IS_BYPASS;
        if (i == Style || i == PredelaySync || (i >= globals && (i - globals) % fields == Type))
            info->flags |= CLAP_PARAM_IS_ENUM;
        info->min_value = p.min;
        info->max_value = p.max;
        info->default_value = p.initial;
        std::snprintf(info->name, sizeof(info->name), "%s", p.name);
        if (i >= globals)
            std::snprintf(info->module, sizeof(info->module), "%s/Band %u",
                          i >= globals + bands * fields ? "Post EQ" : "Decay EQ",
                          (i - globals) / fields % bands + 1);

        return true;
    }
    bool paramsValue(clap_id id, double *value) noexcept override {
        const int i = indexForId(id);
        if (i < 0 || !value)
            return false;
        Values current;
        snapshot(current);
        *value = current[i];
        return true;
    }
    bool paramsValueToText(clap_id id, double value, char *text, uint32_t size) noexcept override {
        const int i = indexForId(id);
        if (i < 0 || !text || size == 0 || !std::isfinite(value))
            return false;
        format(i, parameter(i).constrain(value), text, size);
        return true;
    }
    bool paramsTextToValue(clap_id id, const char *text, double *value) noexcept override {
        const int i = indexForId(id);
        return i >= 0 && text && value && parse(i, text, *value);
    }
    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override {
        consumeState();
        consumeUi(out);
        if (in)
            for (uint32_t i = 0, n = in->size(in); i < n; ++i)
                event(in->get(in, i));
        publish();
    }
    bool implementRemoteControls() const noexcept override { return true; }
    uint32_t remoteControlsPageCount() noexcept override { return 3; }
    bool remoteControlsPageGet(uint32_t i, clap_remote_controls_page *p) noexcept override {
        if (i >= 3 || !p)
            return false;
        *p = {};
        p->page_id = i;
        const char *names[]{"Space", "Dynamics / timing", "Gain / mix"};
        std::snprintf(p->section_name, sizeof(p->section_name), "Reverb");
        std::snprintf(p->page_name, sizeof(p->page_name), "%s", names[i]);
        const unsigned ids[3][8]{
            {Space, DecayRate, Character, Thickness, Distance, Brightness, Width, Style},
            {Predelay, PredelaySync, PredelayOffset, Ducking, AutoGate, GateHold, Freeze, Bypass},
            {Input, Output, Mix, MixLock, Width, Ducking, Freeze, Bypass}};
        for (unsigned k = 0; k < 8; ++k)
            p->param_ids[k] = ids[i][k];
        return true;
    }
    bool implementsState() const noexcept override { return true; }
    static constexpr size_t stateSize = 16 + parameterCount * 12 + 8;
    bool stateSave(const clap_ostream *stream) noexcept override {
        Values values;
        unsigned revision;
        if (!snapshot(values, &revision))
            return false;
        std::array<uint8_t, stateSize> data{};
        std::memcpy(data.data(), "OFRVSTAT", 8);
        plugin::put32(data.data() + 8, 2);
        plugin::put32(data.data() + 12, parameterCount);
        for (unsigned i = 0; i < parameterCount; ++i) {
            plugin::put32(data.data() + 16 + i * 12, parameter(i).id);
            plugin::put64(data.data() + 20 + i * 12, std::bit_cast<uint64_t>(values[i]));
        }
        plugin::put32(data.data() + stateSize - 8, revision);
        plugin::put32(data.data() + stateSize - 4, plugin::checksum(data.data(), stateSize - 4));
        return plugin::writeAll(stream, data.data(), data.size());
    }
    bool stateLoad(const clap_istream *stream) noexcept override {
        std::array<uint8_t, stateSize> data{};
        if (!plugin::readAll(stream, data.data(), 16) || std::memcmp(data.data(), "OFRVSTAT", 8) ||
            plugin::get32(data.data() + 12) != parameterCount)
            return false;
        const auto schema = plugin::get32(data.data() + 8);
        if (schema != 1 && schema != 2)
            return false;
        const size_t size = schema == 1 ? stateSize - 4 : stateSize;
        if (!plugin::readAll(stream, data.data() + 16, size - 16) ||
            plugin::get32(data.data() + size - 4) != plugin::checksum(data.data(), size - 4))
            return false;
        Snapshot s{};
        s.revision = schema == 1 ? 1 : plugin::get32(data.data() + stateSize - 8);
        if (s.revision != 1 && s.revision != 2)
            return false;
        s.serial = requested_.serial + 1;
        for (unsigned i = 0; i < parameterCount; ++i) {
            const auto p = parameter(i);
            const double v = std::bit_cast<double>(plugin::get64(data.data() + 20 + i * 12));
            if (plugin::get32(data.data() + 16 + i * 12) != p.id || !std::isfinite(v) ||
                v < p.min || v > p.max || (p.stepped && v != std::trunc(v)))
                return false;
            s.values[i] = v;
        }
        if (!pending_.push(s))
            return false;
        requested_ = s;
        uiDesiredSerial_.fill(0);
        if (!isActive())
            consumeState();
        const auto *params =
            static_cast<const clap_host_params *>(host_->get_extension(host_, CLAP_EXT_PARAMS));
        if (params && params->rescan)
            params->rescan(host_, CLAP_PARAM_RESCAN_VALUES);
        if (isActive())
            host_->request_process(host_);
        return true;
    }
};

uint32_t CLAP_ABI count(const clap_plugin_factory *) {
    return 1;
}
const clap_plugin_descriptor *CLAP_ABI describe(const clap_plugin_factory *, uint32_t i) {
    return i == 0 ? &descriptor : nullptr;
}
const clap_plugin *CLAP_ABI create(const clap_plugin_factory *, const clap_host *host,
                                   const char *id) {
    if (!host || !id || !clap_version_is_compatible(host->clap_version) ||
        std::strcmp(id, descriptor.id))
        return nullptr;
    try {
        return (new Plugin(host))->clapPlugin();
    } catch (...) {
        return nullptr;
    }
}
const clap_plugin_factory factory{count, describe, create};
bool CLAP_ABI entryInit(const char *) {
    return true;
}
void CLAP_ABI entryDeinit() {}
const void *CLAP_ABI getFactory(const char *id) {
    return id && !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr;
}
} // namespace openfilter::reverb
extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry{
    CLAP_VERSION, openfilter::reverb::entryInit, openfilter::reverb::entryDeinit,
    openfilter::reverb::getFactory};
