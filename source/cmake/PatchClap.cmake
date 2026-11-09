find_program(GIT_PROGRAM git REQUIRED)
execute_process(COMMAND "${GIT_PROGRAM}" apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIRECTORY}" RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
if(NOT already_applied EQUAL 0)
    execute_process(COMMAND "${GIT_PROGRAM}" apply "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIRECTORY}" COMMAND_ERROR_IS_FATAL ANY)
endif()
