cmake_minimum_required(VERSION 3.16)

project("BitSimulator")

add_subdirectory("BitSimulator")

# Copy yosys_bundle to build directory
add_custom_target(copy_yosys_bundle ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/BitSimulator/yosys_bundle
    COMMAND cp -r ${CMAKE_SOURCE_DIR}/yosys_bundle ${CMAKE_BINARY_DIR}/BitSimulator
    COMMAND cp ${CMAKE_SOURCE_DIR}/dot ${CMAKE_BINARY_DIR}/BitSimulator
)
