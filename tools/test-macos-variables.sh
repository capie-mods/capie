#!/bin/sh
# Standalone synthetic reader checks; does not build/load the game diagnostic.
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fixture_dir=$(mktemp -d "${TMPDIR:-/tmp}/capie-variables.XXXXXX")
trap 'rm -rf "$fixture_dir"' EXIT HUP INT TERM
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$repo_root/plugin/capie/tests/macos_variables_test.cpp" -o "$fixture_dir/test"
"$fixture_dir/test"
