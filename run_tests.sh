#!/usr/bin/env bash
# Builds and runs the tests (args go to ctest, e.g. -R simulator, -V)
# Env: QT_PREFIX (Qt to use), BITSIM_TEST_DIR (default build/tests), BITSIM_SHOW_GUI=1
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${BITSIM_TEST_DIR:-$root/build/tests}"

configure=(-S "$root" -B "$build" -DBITSIM_BUILD_TESTS=ON)

# Only the first time; an existing build folder keeps its settings
if [[ ! -f "$build/CMakeCache.txt" ]]; then
    configure+=(-DCMAKE_BUILD_TYPE=Release)
    if command -v ninja >/dev/null 2>&1; then configure+=(-G Ninja); fi

    qt="${QT_PREFIX:-}"
    if [[ -z "$qt" ]]; then
        qt="$(ls -d "$HOME"/Qt/*/gcc_64 2>/dev/null | sort -V | tail -n 1 || true)"
    fi
    if [[ -n "$qt" ]]; then
        echo "Using Qt from $qt"
        configure+=("-DCMAKE_PREFIX_PATH=$qt")
    fi
fi

if [[ "${BITSIM_SHOW_GUI:-0}" == "1" ]]; then
    configure+=(-DBITSIM_TESTS_OFFSCREEN=OFF)
else
    configure+=(-DBITSIM_TESTS_OFFSCREEN=ON)
fi

cmake "${configure[@]}"
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure "$@"
