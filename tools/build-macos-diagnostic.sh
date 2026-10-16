#!/bin/bash
# Build a read-only opt-in x86_64 probe; never launches a target.
set -euo pipefail
if [[ $# != 1 || "$1" != /* ]]; then
    echo 'usage: tools/build-macos-diagnostic.sh /absolute/new-output-directory' >&2
    exit 2
fi
repo="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -m 700 "$1"
/usr/bin/xcrun clang++ -std=c++20 -Wall -Wextra -Werror -arch x86_64 -dynamiclib -fvisibility=hidden \
    "$repo/plugin/capie/src/diagnostics/macos_probe.cpp" -o "$1/capie_diagnostic.dylib"
printf 'Built read-only diagnostic: %s/capie_diagnostic.dylib\n' "$1"
