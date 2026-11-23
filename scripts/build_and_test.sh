#!/usr/bin/env bash
# Configures, builds, and tests plugin-system-cpp from a clean clone:
#   - Debug build + CTest (full GoogleTest suite: ABI layout, loader
#     success/failure paths, lifecycle, directory scan, unload order)
#   - AddressSanitizer + UndefinedBehaviorSanitizer build + CTest
#   - ThreadSanitizer build + CTest (provided for completeness; the
#     host/plugin interaction is single-threaded, see README
#     "Limitations", so this is not expected to find anything)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found on PATH; bootstrapping a project-local copy via pip (no sudo, no global install)..."
  python3 -m venv "$ROOT_DIR/.venv"
  "$ROOT_DIR/.venv/bin/pip" install --quiet --upgrade pip
  "$ROOT_DIR/.venv/bin/pip" install --quiet cmake ninja
  export PATH="$ROOT_DIR/.venv/bin:$PATH"
fi

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="Ninja"
fi

build_and_test() {
  local dir="$1"
  shift
  echo "=== Configuring $dir ($*) ==="
  cmake -S . -B "$dir" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Debug "$@"
  echo "=== Building $dir ==="
  cmake --build "$dir" --
  echo "=== Testing $dir ==="
  ctest --test-dir "$dir" --output-on-failure
}

build_and_test build
build_and_test build-asan -DENABLE_ASAN=ON
build_and_test build-tsan -DENABLE_TSAN=ON

echo
echo "=== Demo: load a single plugin ==="
./build/plugin_host load ./build/reverse_string.dylib "hello world" || ./build/plugin_host load ./build/reverse_string.so "hello world"

echo
echo "=== Demo: scan a directory of good plugins ==="
./build/plugin_host scan ./build/good_plugins "hello"

echo
echo "=== Demo: scan a mixed directory (good + broken fixtures) ==="
./build/plugin_host scan ./build/mixed_plugins "hello"

echo
echo "All builds and tests passed."
