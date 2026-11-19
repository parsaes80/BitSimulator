# cmake -DSOURCE=<checkout> -DPATCH=<file> -P apply_patch.cmake; once ("ignore = dirty" hides it)

find_package(Git REQUIRED)

# A Windows checkout may have CRLF
set(apply_options --ignore-whitespace --whitespace=nowarn)

execute_process(
    COMMAND ${GIT_EXECUTABLE} apply ${apply_options} --reverse --check ${PATCH}
    WORKING_DIRECTORY ${SOURCE}
    RESULT_VARIABLE not_applied
    OUTPUT_QUIET ERROR_QUIET)
if(not_applied EQUAL 0)
    message(STATUS "Graphviz: ${PATCH} is already applied")
    return()
endif()

execute_process(
    COMMAND ${GIT_EXECUTABLE} apply ${apply_options} ${PATCH}
    WORKING_DIRECTORY ${SOURCE}
    RESULT_VARIABLE failed
    ERROR_VARIABLE errors)
if(failed)
    message(FATAL_ERROR "Graphviz: could not apply ${PATCH} to ${SOURCE}:\n${errors}\n"
                        "Is third_party/graphviz checked out at the commit the patch was made for?")
endif()
message(STATUS "Graphviz: applied ${PATCH}")
