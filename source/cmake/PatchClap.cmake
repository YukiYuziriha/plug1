find_program(GIT_PROGRAM git REQUIRED)
# The subsequent sidechain patch deliberately changes the original port hunks.
# FetchContent may rerun this step on reconfigure; do not try to apply the old
# hunks over that known successor. The dependency itself remains pinned.
file(READ "${SOURCE_DIRECTORY}/src/wrapper/clap-juce-wrapper.cpp" wrapper_contents)
string(FIND "${wrapper_contents}" "// Vocal Stack optional auxiliary layouts v1" sidechain_patch)
if(NOT sidechain_patch EQUAL -1)
    string(FIND "${wrapper_contents}" "CLAP_PARAM_IS_BYPASS" bypass_patch)
    string(FIND "${wrapper_contents}" "written <= 0" stream_patch)
    if(bypass_patch EQUAL -1 OR stream_patch EQUAL -1)
        message(FATAL_ERROR "Incomplete pinned CLAP patch chain; refusing to continue")
    endif()
    return()
endif()
execute_process(COMMAND "${GIT_PROGRAM}" apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIRECTORY}" RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
if(NOT already_applied EQUAL 0)
    execute_process(COMMAND "${GIT_PROGRAM}" apply "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIRECTORY}" COMMAND_ERROR_IS_FATAL ANY)
endif()
