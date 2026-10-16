#!/bin/bash
# Owned x86_64 quiescent-code detours only; no game, loader, or live-thread claims.
set -euo pipefail
if [[ "$(uname -s)" != Darwin ]]; then
    echo 'error: macOS required' >&2
    exit 1
fi
repo="$(cd "$(dirname "$0")/.." && pwd)"
output="$(mktemp -d "${TMPDIR:-/tmp}/capie-quiescent-detour.XXXXXX")"
printf 'Artifacts: %s\n' "$output"
/usr/bin/xcrun clang++ -std=c++20 -Wall -Wextra -Werror -arch x86_64 \
    "$repo/plugin/capie/tests/macos_patch_test.cpp" -o "$output/detour-test"
python3 "$repo/tools/check-macos-detour-prefixes.py"
"$output/detour-test"
printf '%s\n' 'PASS: owned x86_64 code only; caller guarantees quiescence. No game hooks or loader tested.'
