cmake_minimum_required(VERSION 3.16)

project("BitSimulator")

add_subdirectory("BitSimulator")

# Copy yosys to build directory
add_custom_target(copy_yosys_bundle ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/BitSimulator/oss-cad-suite
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/oss-cad-suite ${CMAKE_BINARY_DIR}/BitSimulator/oss-cad-suite
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/dot ${CMAKE_BINARY_DIR}/BitSimulator/dot
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/script.bat ${CMAKE_BINARY_DIR}/BitSimulator/script.bat
)