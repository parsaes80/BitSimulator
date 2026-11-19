# Built with -DBITSIM_BUILD_TESTS=ON; run with the "check" target or ./run_tests.sh

cmake_minimum_required(VERSION 3.16)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# The app's sources minus main.cpp, as a library: each test has its own main()
set(BITSIM_APP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/BitSimulator)
file(GLOB BITSIM_APP_SOURCES CONFIGURE_DEPENDS
    ${BITSIM_APP_SOURCE_DIR}/*.cpp
    ${BITSIM_APP_SOURCE_DIR}/*.h
    ${BITSIM_APP_SOURCE_DIR}/*.ui
)
list(FILTER BITSIM_APP_SOURCES EXCLUDE REGEX "/main\\.cpp$")

add_library(bitsim_under_test STATIC ${BITSIM_APP_SOURCES})
# AUTOUIC changes to ui_BitSimulator.h aren't always noticed; a stale layout crashes the tests
set_source_files_properties(${BITSIM_APP_SOURCE_DIR}/MainWindow.cpp PROPERTIES OBJECT_DEPENDS ${BITSIM_APP_SOURCE_DIR}/BitSimulator.ui)
target_link_libraries(bitsim_under_test PUBLIC Qt6::Widgets)

# For the generated ui_BitSimulator.h
get_property(BITSIM_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(BITSIM_MULTI_CONFIG)
    set(BITSIM_UI_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/bitsim_under_test_autogen/include_$<CONFIG>)
else()
    set(BITSIM_UI_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/bitsim_under_test_autogen/include)
endif()
target_include_directories(bitsim_under_test PUBLIC ${BITSIM_APP_SOURCE_DIR} ${BITSIM_UI_INCLUDE_DIR})

# Run where the app runs, so the HDL tests find the tools
set(BITSIM_TEST_WORKING_DIR ${CMAKE_BINARY_DIR}/BitSimulator)

# OFF to watch the tests drive a real window
option(BITSIM_TESTS_OFFSCREEN "Run the GUI tests without showing windows" ON)

set(BITSIM_TESTS "")
function(bitsim_add_test name)
    # Per test program: in a static library the images would need Q_INIT_RESOURCE
    add_executable(${name} ${name}.cpp TestSupport.h ${BITSIM_APP_SOURCE_DIR}/theme/theme.qrc)
    set_target_properties(${name} PROPERTIES AUTOUIC OFF)
    # See above
    set_source_files_properties(${name}.cpp PROPERTIES OBJECT_DEPENDS ${BITSIM_APP_SOURCE_DIR}/BitSimulator.ui)
    target_link_libraries(${name} PRIVATE bitsim_under_test Qt6::Test)
    add_dependencies(${name} copy_yosys_bundle copy_graphviz_dot)

    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${BITSIM_TEST_WORKING_DIR})
    set_tests_properties(${name} PROPERTIES TIMEOUT 600)
    if(BITSIM_TESTS_OFFSCREEN)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    set(BITSIM_TESTS ${BITSIM_TESTS} ${name} PARENT_SCOPE)
endfunction()

bitsim_add_test(tst_itemprops)      # the property and source helpers in Items.h
bitsim_add_test(tst_simulator)      # the simulation core, without any GUI
bitsim_add_test(tst_scene)          # components, editing rules, simulation state, drawing -> netlist
bitsim_add_test(tst_panel)          # the Properties panel, through the real window
bitsim_add_test(tst_lifecycle)      # Start / Pause / Clear / Compile with the simulator thread
bitsim_add_test(tst_hdlcompiler)    # Verilog -> component graph (needs yosys and dot; skipped otherwise)
bitsim_add_test(tst_theme)          # light and dark mode, the theme button, the canvas dots
bitsim_add_test(tst_waveform)       # the waveforms: recording in the simulator, the history, the view

# "check" builds and runs every test
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS ${BITSIM_TESTS}
    USES_TERMINAL
    COMMENT "Running the BitSimulator tests"
)
