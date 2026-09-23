#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
validator="$repo_dir/build/tools/clap-validator/target/release/clap-validator"
plugin_path="${1:-$repo_dir/build/release/plugins/OpenFilterEQ.clap}"
# Run serially: the denormal timing comparison is noisy under competing tests.
"$validator" validate -j 1 "$plugin_path"
while IFS= read -r seed; do
    [[ -z "$seed" || "$seed" == \#* ]] && continue
    "$validator" fuzz --reproduce "$seed" "$plugin_path"
done < "$repo_dir/tests/fuzz-seeds.txt"
