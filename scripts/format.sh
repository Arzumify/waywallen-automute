#!/usr/bin/env bash
# Format all Rust crates in the waywallen workspace.
# Usage:
#   scripts/format.sh            format in place
#   scripts/format.sh --check    exit non-zero if anything needs reformatting (CI mode)

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

ARGS=()
if [[ "${1:-}" == "--check" ]]; then
    ARGS+=(--check)
fi

cargo fmt --all -- "${ARGS[@]}"
