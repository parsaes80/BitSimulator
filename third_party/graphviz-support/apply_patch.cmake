# Applies PATCH to the git checkout in SOURCE, once: does nothing if it is already applied.
#
#   cmake -DSOURCE=<checkout> -DPATCH=<file.patch> -P apply_patch.cmake
#
# The patch changes the submodule's working tree only (its commit stays the upstream one);
# .gitmodules has "ignore = dirty" so that does not show up as a change in this repository.

find_package(Git REQUIRED)

# --ignore-whitespace: a Windows checkout may have CRLF line endings, the patch has LF
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
