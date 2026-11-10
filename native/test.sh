#!/usr/bin/env bash
# Engine conformance tests. These run on any host with a C++17 compiler; they do
# not need Windows because the engine is pure standard C++.
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-g++}"
mkdir -p build
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
  engine/engine.cpp tests/test_engine.cpp -o build/engine-tests
exec ./build/engine-tests
