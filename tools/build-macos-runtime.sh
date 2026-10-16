#!/bin/bash
# Build the late x86_64 runtime; does not install, launch or patch any process.
set -euo pipefail
if [[ $# -ne 1 || $1 != /* ]]; then
    echo 'usage: tools/build-macos-runtime.sh /absolute/new-output-directory' >&2
    exit 64
fi
repo_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -m 700 -- "$1"
xcrun clang++ -std=c++20 -arch x86_64 -O2 -Wall -Wextra -Werror \
    -Wno-deprecated-declarations -fvisibility=hidden -dynamiclib -undefined dynamic_lookup \
    "$repo_dir/plugin/capie/src/dll/macos_runtime.cpp" -o "$1/capie_runtime.dylib"
shasum -a 256 "$1/capie_runtime.dylib"
