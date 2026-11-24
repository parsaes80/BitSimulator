cmake_minimum_required(VERSION 3.16)

# Browser build: Qt finds Emscripten through EMSDK, so it is set here when the shell has none (before project(), which loads the toolchain)
set(BITSIM_EMSDK "/usr/local/bin/emsdk" CACHE PATH "The emsdk folder with the Emscripten version Qt's WebAssembly kit needs")
if(CMAKE_TOOLCHAIN_FILE MATCHES "/wasm_" AND NOT DEFINED ENV{EMSDK})
    if(NOT EXISTS ${BITSIM_EMSDK}/.emscripten)
        message(FATAL_ERROR "No emsdk in ${BITSIM_EMSDK}. Set BITSIM_EMSDK to the emsdk folder, or set the EMSDK environment variable.")
    endif()
    set(ENV{EMSDK} ${BITSIM_EMSDK})
endif()

project(BitSimulator VERSION 3.2)

add_subdirectory("BitSimulator")

# Tools from third_party/ go next to the executable, which finds them by relative path
set(APP_DIR ${CMAKE_BINARY_DIR}/BitSimulator)
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

if(EMSCRIPTEN)
    # The browser build runs yosys and dot in Web Workers (GraphvizWasm.cmake), not as programs
elseif(WIN32)
    # Windows: OSS CAD Suite yosys, sv2v, and GHDL's own build (ghdl/bin and ghdl/lib keep their layout)
    add_custom_target(copy_yosys_bundle ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${APP_DIR}/oss-cad-suite ${APP_DIR}/ghdl ${APP_DIR}/dot
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${THIRD_PARTY_DIR}/oss-cad-suite ${APP_DIR}/oss-cad-suite
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${THIRD_PARTY_DIR}/ghdl ${APP_DIR}/ghdl
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THIRD_PARTY_DIR}/sv2v/windows/sv2v.exe ${APP_DIR}/sv2v.exe
    )
else()
    # Linux: chmod, since a commit from Windows can drop the executable bit
    add_custom_target(copy_yosys_bundle ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${APP_DIR}/yosys_bundle
        COMMAND cp -r ${THIRD_PARTY_DIR}/yosys_bundle ${APP_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THIRD_PARTY_DIR}/sv2v/linux/sv2v ${APP_DIR}/sv2v
        COMMAND chmod -R a+x ${APP_DIR}/yosys_bundle/bin ${APP_DIR}/yosys_bundle/libexec ${APP_DIR}/sv2v
                ${APP_DIR}/yosys_bundle/lib/ld-linux-x86-64.so.2 ${APP_DIR}/yosys_bundle/lib/yosys-abc
    )
endif()

# The license texts go with the app (next to the executable, or the page); the About dialog links them
add_custom_target(copy_licenses ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/licenses ${APP_DIR}/licenses
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/LICENSE ${APP_DIR}/licenses/BitSimulator-MIT.txt
)
add_dependencies(BitSimulator copy_licenses)

if(EMSCRIPTEN)
    # dot and yosys for the page's Web Workers
    include(${THIRD_PARTY_DIR}/graphviz-support/GraphvizWasm.cmake)
    add_dependencies(BitSimulator copy_browser_tools)
else()
    # Graphviz dot from the submodule (the first build takes a few minutes)
    include(${THIRD_PARTY_DIR}/graphviz-support/Graphviz.cmake)

    # So building just the app still puts the tools in place
    add_dependencies(BitSimulator copy_yosys_bundle copy_graphviz_dot)
endif()

# Tests: -DBITSIM_BUILD_TESTS=ON, then the "check" target
option(BITSIM_BUILD_TESTS "Build the automated tests (needs the Qt Test module)" OFF)
if(BITSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
