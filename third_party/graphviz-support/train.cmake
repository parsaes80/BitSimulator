# PGO training: cmake -DDOT=<instrumented dot> -DGRAPHS=<.dot folder> -DPROFILE_DIR=<dir> -P train.cmake

file(REMOVE_RECURSE ${PROFILE_DIR})   # profiles of an older build would not match this one
file(MAKE_DIRECTORY ${PROFILE_DIR})

file(GLOB graphs ${GRAPHS}/*.dot)
foreach(graph IN LISTS graphs)
    execute_process(
        COMMAND ${DOT} -Tdot -Kdot -Grankdir=LR ${graph}
        OUTPUT_QUIET ERROR_QUIET
        RESULT_VARIABLE failed)
    if(failed)
        message(FATAL_ERROR "Graphviz: the training run failed on ${graph}")
    endif()
endforeach()
message(STATUS "Graphviz: trained on ${graphs}")
