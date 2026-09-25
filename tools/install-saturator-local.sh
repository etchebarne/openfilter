#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source_path="$repo_dir/build/release/plugins/OpenFilterSaturator.clap"
destination_dir="${CLAP_INSTALL_DIR:-$HOME/.clap}"
test -f "$source_path"
mkdir -p "$destination_dir"
# Only this suite artifact is replaced; the EQ and other plugins are untouched.
temporary_path="$(mktemp "$destination_dir/.OpenFilterSaturator.XXXXXX")"
trap 'rm -f -- "$temporary_path"' EXIT
install -m 755 "$source_path" "$temporary_path"
mv -f -- "$temporary_path" "$destination_dir/OpenFilterSaturator.clap"
printf 'Installed %s\n' "$destination_dir/OpenFilterSaturator.clap"
