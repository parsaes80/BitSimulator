# Profile-guided optimization, training run: lays out every circuit in GRAPHS with the
# instrumented DOT, which records how often each part of it runs into PROFILE_DIR.
#
#   cmake -DDOT=<instrumented dot> -DGRAPHS=<folder of .dot files> -DPROFILE_DIR=<folder> -P train.cmake
#
# The circuits are what BitSimulator hands dot: yosys "show" output of a few designs,
# laid out left to right, exactly like HDLCompiler does it.

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
