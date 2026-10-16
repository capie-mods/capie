#!/bin/bash
# Runs portable logic and the standalone macOS memory backend; never starts HOI4.
set -euo pipefail

if [[ $(uname -s) != Darwin ]]; then
    echo 'error: macOS is required' >&2
    exit 1
fi
if [[ $# -ne 0 ]]; then
    echo 'usage: tools/test-macos-offline.sh' >&2
    exit 2
fi
repo_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
out_dir=$(mktemp -d "${TMPDIR:-/tmp}/capie-offline.XXXXXX")
# Retain binaries for inspection; this script never deletes user files.
echo "Artifacts: $out_dir"
echo 'Scope: grammar/alphabet, country protocol, editor input, macOS memory/string/widget adapters, owned strings, Mach-O/profile/pin-verifier checks.'
echo 'Not tested: dylibs, full game ABI/pin coverage, hooks, loader, installer, or in-game behavior.'

architectures=(x86_64)
if [[ $(uname -m) == arm64 ]]; then
    architectures=(arm64 x86_64)
fi
sources=(
    public_names_test.cpp
    legacy/protocol_test.cpp
    legacy/editor_input_test.cpp
    macos_memory_test.cpp
    macos_string_test.cpp
    macos_image_test.cpp
    macos_widgets_test.cpp
    macos_owned_string_test.cpp
    macos_pins_test.cpp
    macos_localisation_test.cpp
    macos_variables_test.cpp
    macos_name_layer_test.cpp
    macos_portrait_layer_test.cpp
    macos_portrait_hooks_test.cpp
    macos_flag_hooks_test.cpp
    macos_records_test.cpp
    macos_name_hooks_test.cpp
    legacy/trace_ring_test.cpp
    macos_log_test.cpp
    macos_shared_widgets_test.cpp
    macos_caption_focus_test.cpp
    macos_game_calls_test.cpp
    macos_editor_lifecycle_test.cpp
    macos_transport_test.cpp
    macos_hook_prefix_test.cpp
    macos_country_test.cpp
    macos_editor_composition_test.cpp
    macos_activation_checks_test.cpp
)
for architecture in "${architectures[@]}"; do
    mkdir -p "$out_dir/$architecture"
    for source in "${sources[@]}"; do
        name=$(basename "$source" .cpp)
        binary="$out_dir/$architecture/$name"
        echo "Build/run: $architecture $name"
        xcrun clang++ -std=c++20 -Wall -Wextra -Werror -arch "$architecture" \
            "$repo_dir/plugin/capie/tests/$source" -o "$binary"
        # Executing the x86_64 binary on Apple Silicon exercises Rosetta when available.
        # A missing runtime is an error, never a silently skipped success.
        if [[ $name == macos_image_test ]]; then
            game_file="${HOI4_MACOS_BINARY:-$HOME/Library/Application Support/Steam/steamapps/common/Hearts of Iron IV/hoi4.app/Contents/MacOS/hoi4}"
            if [[ -f $game_file ]]; then
                "$binary" "$game_file"
            elif [[ -n ${HOI4_MACOS_BINARY+x} ]]; then
                echo "error: explicitly provided game file is missing: $game_file" >&2
                exit 1
            else
                "$binary" # Explicit SKIP inside the test; synthetic fixtures still run.
            fi
        else
            "$binary"
        fi
    done
done
echo "PASS: ${#sources[@]} offline suites per architecture: ${architectures[*]}"
