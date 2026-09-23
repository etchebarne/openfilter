#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
validator_dir="$repo_dir/build/tools/clap-validator"
validator_revision=b2f1d9b79b1d264a5747f46707d72b1aa40a02ef
if [[ ! -d "$validator_dir/.git" ]]; then
    git clone https://github.com/free-audio/clap-validator.git "$validator_dir"
fi
git -C "$validator_dir" checkout --detach "$validator_revision"
cargo build --release --locked --manifest-path "$validator_dir/Cargo.toml"
