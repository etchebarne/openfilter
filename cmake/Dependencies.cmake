include(FetchContent)
# MIT licensed. Update deliberately and rerun the host-contract suite.
FetchContent_Declare(clap GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG a47f6badb49d948fd009998f28309cdab78979c9 SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(clap)
FetchContent_Declare(clap_helpers GIT_REPOSITORY https://github.com/free-audio/clap-helpers.git
  GIT_TAG 55a5dd5d1db9c87b32f407e387f64676d27e10b1 SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(clap_helpers)

# Pugl supplies native window embedding; only its Linux window backend is built.
FetchContent_Declare(pugl GIT_REPOSITORY https://github.com/lv2/pugl.git
  GIT_TAG b7637149ebe53124e5be90559e02a0185bbcbd73 SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(pugl)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
add_library(openfilter_pugl STATIC ${pugl_SOURCE_DIR}/src/common.c
  ${pugl_SOURCE_DIR}/src/internal.c ${pugl_SOURCE_DIR}/src/x11.c
  ${pugl_SOURCE_DIR}/src/x11_stub.c)
target_include_directories(openfilter_pugl SYSTEM PUBLIC ${pugl_SOURCE_DIR}/include)
target_compile_definitions(openfilter_pugl PRIVATE PUGL_INTERNAL PUGL_STATIC
  USE_XRANDR=0 USE_XSYNC=0 USE_XCURSOR=0)
target_link_libraries(openfilter_pugl PUBLIC PkgConfig::CAIRO PkgConfig::X11 m)
set_target_properties(openfilter_pugl PROPERTIES C_VISIBILITY_PRESET hidden)
