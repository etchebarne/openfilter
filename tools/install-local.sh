#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source_path="${1:-$repo_dir/build/release/plugins/OpenFilterEQ.clap}"
destination_dir="${CLAP_INSTALL_DIR:-$HOME/.clap}"
test -f "$source_path"
mkdir -p "$destination_dir"
# Atomic replacement avoids exposing a partially copied binary to a DAW scanner.
temporary_path="$(mktemp "$destination_dir/.OpenFilterEQ.XXXXXX")"
trap 'rm -f -- "$temporary_path"' EXIT
install -m 755 "$source_path" "$temporary_path"
mv -f -- "$temporary_path" "$destination_dir/OpenFilterEQ.clap"
printf 'Installed %s\n' "$destination_dir/OpenFilterEQ.clap"
