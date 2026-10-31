#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
  -DPERFORMANCE_DATA_NO_JSON=1 \
  "$repo_dir/tests/performance_data_native.cpp" \
  "$repo_dir/src/performance_data.cpp" \
  -I"$repo_dir/src" \
  -o "$build_dir/performance_data_native"

"$build_dir/performance_data_native"
