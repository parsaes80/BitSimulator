# Automated tests for BitSimulator. Only built when BITSIM_BUILD_TESTS is ON (see the top-level CMakeLists.txt).
#
#   cmake -S . -B build/tests -DBITSIM_BUILD_TESTS=ON
#   cmake --build build/tests --target check        # build everything and run all tests
#
# or simply ./run_tests.sh on Linux.

cmake_minimum_required(VERSION 3.16)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# The application's own sources, compiled into a library the tests link against.
# Everything except main.cpp: each test brings its own main().
set(BITSIM_APP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/BitSimulator)
file(GLOB BITSIM_APP_SOURCES CONFIGURE_DEPENDS
    ${BITSIM_APP_SOURCE_DIR}/*.cpp
    ${BITSIM_APP_SOURCE_DIR}/*.h
    ${BITSIM_APP_SOURCE_DIR}/*.ui
)
list(FILTER BITSIM_APP_SOURCES EXCLUDE REGEX "/main\\.cpp$")

add_library(bitsim_under_test STATIC ${BITSIM_APP_SOURCES})
target_link_libraries(bitsim_under_test PUBLIC Qt6::Widgets)

# MainWindow.h includes the generated ui_BitSimulator.h, so the tests need the folder it is generated into
get_property(BITSIM_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(BITSIM_MULTI_CONFIG)
    set(BITSIM_UI_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/bitsim_under_test_autogen/include_$<CONFIG>)
else()
    set(BITSIM_UI_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/bitsim_under_test_autogen/include)
endif()
target_include_directories(bitsim_under_test PUBLIC ${BITSIM_APP_SOURCE_DIR} ${BITSIM_UI_INCLUDE_DIR})

# Tests run where the application runs, so the HDL tests find yosys (and dot on Windows) next to them
set(BITSIM_TEST_WORKING_DIR ${CMAKE_BINARY_DIR}/BitSimulator)

# Set to OFF to watch the tests drive a real window
option(BITSIM_TESTS_OFFSCREEN "Run the GUI tests without showing windows" ON)

set(BITSIM_TESTS "")
function(bitsim_add_test name)
    add_executable(${name} ${name}.cpp TestSupport.h)
    set_target_properties(${name} PROPERTIES AUTOUIC OFF)
    target_link_libraries(${name} PRIVATE bitsim_under_test Qt6::Test)
    add_dependencies(${name} copy_yosys_bundle)

    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${BITSIM_TEST_WORKING_DIR})
    set_tests_properties(${name} PROPERTIES TIMEOUT 600)
    if(BITSIM_TESTS_OFFSCREEN)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    set(BITSIM_TESTS ${BITSIM_TESTS} ${name} PARENT_SCOPE)
endfunction()

bitsim_add_test(tst_itemprops)      # helper functions in ItemProps.h
bitsim_add_test(tst_simulator)      # the simulation core, without any GUI
bitsim_add_test(tst_scene)          # components, editing rules, simulation state, drawing -> netlist
bitsim_add_test(tst_panel)          # the Properties panel, through the real window
bitsim_add_test(tst_lifecycle)      # Start / Pause / Clear / Compile with the simulator thread
bitsim_add_test(tst_hdlcompiler)    # Verilog -> component graph (needs yosys and dot; skipped otherwise)
bitsim_add_test(tst_theme)          # light and dark mode, the theme button, the canvas dots

# "check" builds every test and runs them all; usable from any IDE as a normal build target
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS ${BITSIM_TESTS}
    USES_TERMINAL
    COMMENT "Running the BitSimulator tests"
)
