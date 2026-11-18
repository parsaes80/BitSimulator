cmake_minimum_required(VERSION 3.16)

project(BitSimulator VERSION 2.0)

add_subdirectory("BitSimulator")

# Copy the external tools next to the executable (paths are resolved relative to it at runtime).
# Their sources are all in third_party/; next to the executable they keep their folder names.
set(APP_DIR ${CMAKE_BINARY_DIR}/BitSimulator)
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

if(WIN32)
    # Windows: yosys from the bundled OSS CAD Suite (yosys.exe and its DLLs in oss-cad-suite/bin), and sv2v
    add_custom_target(copy_yosys_bundle ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${APP_DIR}/oss-cad-suite ${APP_DIR}/dot ${APP_DIR}/script.bat
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${THIRD_PARTY_DIR}/oss-cad-suite ${APP_DIR}/oss-cad-suite
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THIRD_PARTY_DIR}/sv2v/windows/sv2v.exe ${APP_DIR}/sv2v.exe
    )
else()
    # Linux: bundled yosys, and sv2v.
    # The copies are made executable here: a commit from Windows can record the originals without the
    # executable bit, and copy_if_different does not copy sv2v again when only its mode changed.
    add_custom_target(copy_yosys_bundle ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${APP_DIR}/yosys_bundle
        COMMAND cp -r ${THIRD_PARTY_DIR}/yosys_bundle ${APP_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THIRD_PARTY_DIR}/sv2v/linux/sv2v ${APP_DIR}/sv2v
        COMMAND chmod -R a+x ${APP_DIR}/yosys_bundle/bin ${APP_DIR}/sv2v
    )
endif()

# Graphviz dot, built from the third_party/graphviz submodule (the first build takes a few minutes)
include(${THIRD_PARTY_DIR}/graphviz-support/Graphviz.cmake)

# Make the app depend on the tools so building just the BitSimulator target still puts them in place
add_dependencies(BitSimulator copy_yosys_bundle copy_graphviz_dot)

# Automated tests are optional: configure with -DBITSIM_BUILD_TESTS=ON, then build the "check" target
option(BITSIM_BUILD_TESTS "Build the automated tests (needs the Qt Test module)" OFF)
if(BITSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
