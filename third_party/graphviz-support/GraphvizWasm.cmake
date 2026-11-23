# Browser build: dot and YoWASP's yosys run in Web Workers next to the page (BitSimulator/web/*-worker.js)

include(ExternalProject)

set(GRAPHVIZ_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/graphviz)
set(GRAPHVIZ_SUPPORT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(GRAPHVIZ_WASM_DIR ${CMAKE_BINARY_DIR}/graphviz-wasm)
set(GRAPHVIZ_WASM_DOT ${GRAPHVIZ_WASM_DIR}/cmd/dot/dot.js)

# The dot command as an ES module a worker imports: createDot() -> callMain(arguments), with an in-memory FS.
# stable_qsort.c: emscripten's qsort orders equal elements differently, so layouts would not match the desktop dot
set(dot_module_flags
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createDot -sENVIRONMENT=worker -sINVOKE_RUN=0
    -sEXPORTED_RUNTIME_METHODS=callMain,FS -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB
    ${GRAPHVIZ_SUPPORT_DIR}/stable_qsort.c)
list(JOIN dot_module_flags " " dot_module_flags)

ExternalProject_Add(graphviz_wasm
    SOURCE_DIR ${GRAPHVIZ_SOURCE_DIR}
    BINARY_DIR ${GRAPHVIZ_WASM_DIR}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE=${GRAPHVIZ_SOURCE_DIR}
                  -DPATCH=${GRAPHVIZ_SUPPORT_DIR}/ns-compact-arrays.patch
                  -P ${GRAPHVIZ_SUPPORT_DIR}/apply_patch.cmake
    CMAKE_ARGS
        # Emscripten's own toolchain: Qt's needs EMSDK, which this separate configure would not have
        -DCMAKE_TOOLCHAIN_FILE=$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_EXE_LINKER_FLAGS=${dot_module_flags}"
        -DBUILD_SHARED_LIBS=OFF
        -DENABLE_LTDL=OFF
        -DGRAPHVIZ_CLI=ON
        -DMATH_LIB=m                    # emscripten's libm is built in
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
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target dot
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${GRAPHVIZ_WASM_DOT}
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON
)

# YoWASP's yosys (ISC), fetched once with npm
set(YOWASP_VERSION 0.59.1038)   # the yosys version of the desktop bundles
set(YOWASP_DIR ${CMAKE_BINARY_DIR}/yowasp)
if(NOT EXISTS ${YOWASP_DIR}/node_modules/@yowasp/yosys/gen/bundle.js)
    find_program(NPM_EXECUTABLE npm REQUIRED)
    message(STATUS "YoWASP: fetching @yowasp/yosys@${YOWASP_VERSION}")
    file(MAKE_DIRECTORY ${YOWASP_DIR})
    execute_process(
        COMMAND ${NPM_EXECUTABLE} install --ignore-scripts --no-audit --no-fund @yowasp/yosys@${YOWASP_VERSION}
        WORKING_DIRECTORY ${YOWASP_DIR}
        RESULT_VARIABLE failed)
    if(failed)
        message(FATAL_ERROR "YoWASP: could not fetch @yowasp/yosys@${YOWASP_VERSION} with npm")
    endif()
endif()

# Next to the page: the workers, dot (graphviz/) and yosys (yowasp/yosys/gen/)
add_custom_target(copy_browser_tools ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_DIR}/graphviz
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GRAPHVIZ_WASM_DOT} ${GRAPHVIZ_WASM_DIR}/cmd/dot/dot.wasm ${APP_DIR}/graphviz/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${YOWASP_DIR}/node_modules/@yowasp/yosys/gen ${APP_DIR}/yowasp/yosys/gen
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/BitSimulator/web/yosys-worker.js
            ${CMAKE_SOURCE_DIR}/BitSimulator/web/dot-worker.js ${APP_DIR}/
    DEPENDS graphviz_wasm
    COMMENT "Copying dot, YoWASP yosys and their workers next to the page"
)
