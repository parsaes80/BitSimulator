cmake_minimum_required(VERSION 3.16)

project(BitSimulator VERSION 2.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
    main.cpp
    MainWindow.cpp
    MainWindow.h
    CircuitCanvas.cpp
    CircuitCanvas.h
    Items.cpp
    Items.h
    Toolbar.cpp
    Toolbar.h
    PropertiesPanel.cpp
    PropertiesPanel.h
    Waveform.cpp
    Waveform.h
    Theme.cpp
    Theme.h
    simulator.cpp
    simulator.h
    hdlcompiler.cpp
    hdlcompiler.h
    general.h
    BitSimulator.ui
    theme/theme.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(BitSimulator
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}

    )
else()
    if(ANDROID)
        add_library(BitSimulator SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(BitSimulator
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_include_directories(BitSimulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# AUTOUIC changes to ui_BitSimulator.h aren't always noticed; a stale layout crashes the app
set_source_files_properties(main.cpp MainWindow.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/BitSimulator.ui)

target_link_libraries(BitSimulator PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.BitSimulator)
endif()
set_target_properties(BitSimulator PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS BitSimulator
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(BitSimulator)
endif()
