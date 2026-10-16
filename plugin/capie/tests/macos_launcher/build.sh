#!/bin/sh
set -eu
fixture_src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fixture_out=${1:?usage: build.sh NEW_ABSOLUTE_OUTPUT_DIRECTORY}
case "$fixture_out" in /*) ;; *) exit 64 ;; esac
# Refuse to overwrite previous fixtures/results.
mkdir "$fixture_out"
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror "$fixture_src/child_wrapper.cpp" -o "$fixture_out/child-wrapper"
clang -arch x86_64 -Wall -Wextra -Werror "$fixture_src/recorder.c" -o "$fixture_out/recorder"
clang -arch x86_64 -Wall -Wextra -Werror -dynamiclib "$fixture_src/marker.c" -o "$fixture_out/marker.dylib"
mkdir "$fixture_out/enabled" "$fixture_out/disabled" "$fixture_out/invalid"
printf '%s\n' "$fixture_out"
