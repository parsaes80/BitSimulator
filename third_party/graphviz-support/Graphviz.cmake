# Builds Graphviz's `dot` from the third_party/graphviz submodule and puts it next to BitSimulator.
#
# Included from the top-level CMakeLists.txt after APP_DIR is set. It defines the target
# `copy_graphviz_dot`; building it builds dot (the first time only) and copies it to APP_DIR.
#
# What is different from a stock Graphviz build, all for speed and none changing the layout algorithm:
#   - ns-compact-arrays.patch: dot's network simplex runs its main loop on a compact copy of its
#     data. It makes exactly the same choices, so layouts are byte for byte the same; it is about
#     2x faster on circuits, where nearly all of dot's time goes into that loop.
#   - Only dot, linked statically with its layout and output plugins built in: no Graphviz
#     libraries, plugin files or config file to ship.
#   - Release build with link-time optimization (Graphviz turns it on itself for Release).
#   - With GCC: profile-guided optimization, trained on the circuits in training/.
#   - No Pango/Cairo, and on Windows no GDI+ (no-gdiplus.patch): label sizes come from Graphviz's
#     built-in font metrics, so dot needs no font libraries and does not depend on installed fonts.
#     (Windows and Linux can still order tied nodes differently: Graphviz sorts with the C
#     library's qsort, which breaks ties differently on each. That is the same with stock Graphviz.)

include(ExternalProject)

set(GRAPHVIZ_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/graphviz)
set(GRAPHVIZ_SUPPORT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(GRAPHVIZ_BINARY_DIR ${CMAKE_BINARY_DIR}/graphviz)
set(GRAPHVIZ_PROFILE_DIR ${CMAKE_BINARY_DIR}/graphviz-profile)

find_package(Git REQUIRED)

# A clone made without --recursive has an empty submodule folder: fetch it
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

# The parts of Graphviz dot does not need, and the libraries it should not look for
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
# The same compilers as the app
foreach(language C CXX)
    if(CMAKE_${language}_COMPILER)
        list(APPEND GRAPHVIZ_OPTIONS -DCMAKE_${language}_COMPILER=${CMAKE_${language}_COMPILER})
    endif()
endforeach()
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND GRAPHVIZ_OPTIONS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()

# Building Graphviz needs bison and flex. On Windows they come with Graphviz's small
# build-utilities submodule (inside third_party/graphviz), fetched here when missing.
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

# Graphviz requires a getopt library when the C library has no getopt.h, which is the case with
# MSVC (glibc and MinGW have one). Only Graphviz's other tools (gvpr, gc, acyclic, ...) use getopt;
# dot does not, and no source file reads HAVE_GETOPT_H. So answer Graphviz's check up front:
# that skips the search for a getopt library, which only those tools (never built here) would link.
if(MSVC)
    list(APPEND GRAPHVIZ_OPTIONS -DHAVE_GETOPT_H=1)
endif()

# Profile-guided optimization: GCC only (Clang and MSVC handle profiles differently)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    option(BITSIM_DOT_PGO "Build Graphviz dot with profile-guided optimization (about 10% faster)" ON)
else()
    set(BITSIM_DOT_PGO OFF)
endif()

if(BITSIM_DOT_PGO)
    # Both builds happen in the same folder: GCC finds a profile by the object file's path
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
    # Run the instrumented dot on sample circuits, then rebuild it with the recorded profile
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

# Copy dot next to the app on every build (only when it changed), so it is there even after
# the app's folder was cleaned
add_custom_target(copy_graphviz_dot ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GRAPHVIZ_DOT} ${APP_DIR}/
    DEPENDS graphviz_dot
    COMMENT "Copying Graphviz dot next to BitSimulator"
)
