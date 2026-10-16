#!/bin/bash
# Native x86_64 parent and child; no game starts, downloads, signing or installation.
set -euo pipefail
if [[ $# -ne 1 || $1 != /* ]]; then
    echo 'usage: tools/build-macos-launcher.sh /absolute/new-output-directory' >&2
    exit 64
fi
repo_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -m 700 -- "$1"
xcrun clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror -fobjc-arc -framework Foundation \
    "$repo_dir/plugin/capie/src/launcher/macos_child_parent.mm" -o "$1/capie-parent"
xcrun clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    "$repo_dir/plugin/capie/src/launcher/macos_child_main.cpp" -o "$1/capie-child"
shasum -a 256 "$1/capie-parent" "$1/capie-child"
