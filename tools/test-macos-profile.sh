#!/bin/bash
# Generated table and existing pin engine against synthetic memory only.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$(uname -s)" != Darwin ]]; then
    echo 'error: macOS required' >&2
    exit 1
fi
python3 -m unittest discover -s "$repo/docs/macos/re" -p 'test_*pin*.py'
output="$(mktemp -d "${TMPDIR:-/tmp}/capie-profile-test.XXXXXX")"
printf 'Artifacts: %s\n' "$output"
architectures=(x86_64)
if [[ "$(uname -m)" == arm64 ]]; then architectures=(arm64 x86_64); fi
for architecture in "${architectures[@]}"; do
    for suite in macos_profile_test macos_pins_test; do
        /usr/bin/xcrun clang++ -std=c++20 -Wall -Wextra -Werror -arch "$architecture" \
            "$repo/plugin/capie/tests/$suite.cpp" -o "$output/$suite-$architecture"
        "$output/$suite-$architecture"
    done
done
printf '%s\n' 'PASS: generated descriptors and existing pin engine; no game code loaded or executed.'
