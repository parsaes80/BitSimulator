#!/usr/bin/env bash
# Builds BitSimulator with its tests in a separate build folder and runs them.
#
#   ./run_tests.sh                 run everything
#   ./run_tests.sh -R simulator    only tests whose name matches (any ctest option works)
#   ./run_tests.sh -V              show every single check
#
# Environment:
#   QT_PREFIX         Qt installation to use, e.g. ~/Qt/6.12.0/gcc_64 (default: newest under ~/Qt, else the system Qt)
#   BITSIM_TEST_DIR   build folder (default: build/tests, which .gitignore already covers)
#   BITSIM_SHOW_GUI=1 show the windows while the GUI tests drive them (default: run without a display)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${BITSIM_TEST_DIR:-$root/build/tests}"

configure=(-S "$root" -B "$build" -DBITSIM_BUILD_TESTS=ON)

# These only matter the first time; an existing build folder keeps its generator and build type
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
