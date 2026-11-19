# Builds a static, faster dot (ns-compact-arrays.patch, LTO, GCC PGO, no Pango) for copy_graphviz_dot

include(ExternalProject)

set(GRAPHVIZ_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/graphviz)
set(GRAPHVIZ_SUPPORT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(GRAPHVIZ_BINARY_DIR ${CMAKE_BINARY_DIR}/graphviz)
set(GRAPHVIZ_PROFILE_DIR ${CMAKE_BINARY_DIR}/graphviz-profile)

find_package(Git REQUIRED)

# A clone without --recursive has an empty submodule folder
if(NOT EXISTS ${GRAPHVIZ_SOURCE_DIR}/CMakeLists.txt)
    message(STATUS "Graphviz: fetching the third_party/graphviz submodule")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --depth 1 third_party/graphviz
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE failed)
    if(failed)
        message(FATAL_ERROR "Graphviz: could not fetch the submodule. Run in the project folder:\n"
                            "  git submodule update --init --depth 1 third_party/graphviz")
    endif()
endif()

set(GRAPHVIZ_OPTIONS
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DENABLE_LTDL=OFF
    -DGRAPHVIZ_CLI=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_PANGOCAIRO=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_CAIRO=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_Freetype=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_GD=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_GTS=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_ANN=ON
    -DWITH_EXPAT=OFF
    -DWITH_ZLIB=OFF
    -DWITH_GVEDIT=OFF
    -DWITH_SMYRNA=OFF
    -DWITH_GDK=OFF
    -DWITH_GTK=OFF
    -DWITH_GHOSTSCRIPT=OFF
    -DWITH_POPPLER=OFF
    -DWITH_RSVG=OFF
    -DWITH_WEBP=OFF
    -DWITH_X=OFF
    -DENABLE_TCL=OFF
    -DENABLE_SWIG=OFF
    -Dwith_sfdp=OFF
    -Dwith_digcola=OFF
    -Dwith_ipsepcola=OFF
)
foreach(language C CXX)
    if(CMAKE_${language}_COMPILER)
        list(APPEND GRAPHVIZ_OPTIONS -DCMAKE_${language}_COMPILER=${CMAKE_${language}_COMPILER})
    endif()
endforeach()
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND GRAPHVIZ_OPTIONS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()

# bison and flex: on Windows from Graphviz's build-utilities submodule
if(WIN32)
    set(build_utilities windows/dependencies/graphviz-build-utilities)
    if(NOT EXISTS ${GRAPHVIZ_SOURCE_DIR}/${build_utilities}/winflexbison/win_bison.exe)
        message(STATUS "Graphviz: fetching ${build_utilities} (bison and flex for Windows)")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --depth 1 ${build_utilities}
            WORKING_DIRECTORY ${GRAPHVIZ_SOURCE_DIR}
            RESULT_VARIABLE failed)
        if(failed)
            message(FATAL_ERROR "Graphviz: could not fetch ${build_utilities}. Run in third_party/graphviz:\n"
                                "  git submodule update --init --depth 1 ${build_utilities}")
        endif()
    endif()
    list(APPEND GRAPHVIZ_OPTIONS
        -DBISON_EXECUTABLE=${GRAPHVIZ_SOURCE_DIR}/${build_utilities}/winflexbison/win_bison.exe
        -DFLEX_EXECUTABLE=${GRAPHVIZ_SOURCE_DIR}/${build_utilities}/winflexbison/win_flex.exe)
endif()

# MSVC has no getopt.h; only Graphviz tools we don't build use getopt, so skip the search
if(MSVC)
    list(APPEND GRAPHVIZ_OPTIONS -DHAVE_GETOPT_H=1)
endif()

# PGO: GCC only
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    option(BITSIM_DOT_PGO "Build Graphviz dot with profile-guided optimization (about 10% faster)" ON)
else()
    set(BITSIM_DOT_PGO OFF)
endif()

if(BITSIM_DOT_PGO)
    # Both builds in one folder: GCC finds a profile by object path
    set(instrument_flags "-O2 -DNDEBUG -fprofile-generate -fprofile-dir=${GRAPHVIZ_PROFILE_DIR} -fprofile-update=single")
    set(optimize_flags "-O2 -DNDEBUG -fprofile-use -fprofile-dir=${GRAPHVIZ_PROFILE_DIR} -fprofile-partial-training -Wno-missing-profile")
    set(first_build_flags
        -DCMAKE_C_FLAGS_RELEASE=${instrument_flags}
        -DCMAKE_CXX_FLAGS_RELEASE=${instrument_flags}
        -DCMAKE_EXE_LINKER_FLAGS=${instrument_flags})
endif()

get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(multi_config)
    set(GRAPHVIZ_DOT ${GRAPHVIZ_BINARY_DIR}/cmd/dot/Release/dot${CMAKE_EXECUTABLE_SUFFIX})
else()
    set(GRAPHVIZ_DOT ${GRAPHVIZ_BINARY_DIR}/cmd/dot/dot${CMAKE_EXECUTABLE_SUFFIX})
endif()

ExternalProject_Add(graphviz_dot
    SOURCE_DIR ${GRAPHVIZ_SOURCE_DIR}
    BINARY_DIR ${GRAPHVIZ_BINARY_DIR}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE=${GRAPHVIZ_SOURCE_DIR}
                  -DPATCH=${GRAPHVIZ_SUPPORT_DIR}/ns-compact-arrays.patch
                  -P ${GRAPHVIZ_SUPPORT_DIR}/apply_patch.cmake
          COMMAND ${CMAKE_COMMAND} -DSOURCE=${GRAPHVIZ_SOURCE_DIR}
                  -DPATCH=${GRAPHVIZ_SUPPORT_DIR}/no-gdiplus.patch
                  -P ${GRAPHVIZ_SUPPORT_DIR}/apply_patch.cmake
    CMAKE_ARGS ${GRAPHVIZ_OPTIONS} ${first_build_flags}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target dot
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${GRAPHVIZ_DOT}
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON
)

if(BITSIM_DOT_PGO)
    ExternalProject_Add_Step(graphviz_dot train
        COMMAND ${CMAKE_COMMAND} -DDOT=${GRAPHVIZ_DOT} -DGRAPHS=${GRAPHVIZ_SUPPORT_DIR}/training
                -DPROFILE_DIR=${GRAPHVIZ_PROFILE_DIR} -P ${GRAPHVIZ_SUPPORT_DIR}/train.cmake
        DEPENDEES build
        DEPENDS ${GRAPHVIZ_SUPPORT_DIR}/train.cmake
        COMMENT "Graphviz: training dot for profile-guided optimization"
        USES_TERMINAL ON
    )
    ExternalProject_Add_Step(graphviz_dot optimize
        COMMAND ${CMAKE_COMMAND} -DCMAKE_C_FLAGS_RELEASE=${optimize_flags}
                -DCMAKE_CXX_FLAGS_RELEASE=${optimize_flags} -DCMAKE_EXE_LINKER_FLAGS=${optimize_flags} <BINARY_DIR>
        COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target dot
        DEPENDEES train
        COMMENT "Graphviz: rebuilding dot with the recorded profile"
        USES_TERMINAL ON
    )
endif()

# On every build, so dot survives a cleaned app folder
add_custom_target(copy_graphviz_dot ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GRAPHVIZ_DOT} ${APP_DIR}/
    DEPENDS graphviz_dot
    COMMENT "Copying Graphviz dot next to BitSimulator"
)
