cmake_minimum_required(VERSION 3.16)

project("BitSimulator")

add_subdirectory("BitSimulator")

# Copy the external tools next to the executable (paths are resolved relative to it at runtime)
set(APP_DIR ${CMAKE_BINARY_DIR}/BitSimulator)

if(WIN32)
    # Windows: yosys from the bundled OSS CAD Suite (launched via script.bat) and bundled Graphviz dot
    add_custom_target(copy_yosys_bundle ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${APP_DIR}/oss-cad-suite ${APP_DIR}/dot
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/oss-cad-suite ${APP_DIR}/oss-cad-suite
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/dot ${APP_DIR}/dot
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/script.bat ${APP_DIR}/script.bat
    )
else()
    # Linux: bundled yosys; Graphviz dot comes from the system package
    add_custom_target(copy_yosys_bundle ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${APP_DIR}/yosys_bundle
        COMMAND cp -r ${CMAKE_SOURCE_DIR}/yosys_bundle ${APP_DIR}
    )
endif()

# Make the app depend on the bundle so building just the BitSimulator target still copies it
add_dependencies(BitSimulator copy_yosys_bundle)

# Automated tests are optional: configure with -DBITSIM_BUILD_TESTS=ON, then build the "check" target
option(BITSIM_BUILD_TESTS "Build the automated tests (needs the Qt Test module)" OFF)
if(BITSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
