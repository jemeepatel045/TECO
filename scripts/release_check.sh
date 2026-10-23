#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

echo "===== TECO v1.0 release verification ====="

if ! grep -q 'project(TECO VERSION 1.0.0 LANGUAGES CXX)' CMakeLists.txt; then
  echo "ERROR: CMake project version is not 1.0.0" >&2
  exit 1
fi

echo
echo "== Release build =="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2

echo
echo "== CTest =="
ctest --test-dir build --output-on-failure

echo
echo "== Docker resilience =="
if ! command -v docker >/dev/null 2>&1; then
  echo "ERROR: Docker is required for release verification." >&2
  exit 1
fi

docker info >/dev/null
python3 tests/test_docker_resilience.py

echo
echo "===== TECO v1.0 release verification PASSED ====="
