# cli_check.cmake: run one CLI command and check its exit code, stdout and
# stderr apart. CTest's PASS_REGULAR_EXPRESSION reads both streams together
# and ignores the exit code.
#
#   cmake -DCLI=<binary> -DARGS=<arg|arg|...> -DRESULT=<code>
#         [-DSTDOUT_MATCHES=<regex>] [-DSTDOUT_EQUALS=<text>]
#         [-DSTDERR_EQUALS=<text>] -P cli_check.cmake
#
# An unset STDOUT_EQUALS or STDERR_EQUALS means that stream must be empty,
# unless STDOUT_MATCHES is given for stdout.
string(REPLACE "|" ";" args "${ARGS}")
execute_process(COMMAND "${CLI}" ${args}
                RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(failures "")
if(NOT result STREQUAL RESULT)
    string(APPEND failures "exit code ${result}, expected ${RESULT}\n")
endif()
if(DEFINED STDOUT_MATCHES)
    if(NOT out MATCHES "${STDOUT_MATCHES}")
        string(APPEND failures "stdout does not match: ${STDOUT_MATCHES}\n")
    endif()
elseif(NOT out STREQUAL "${STDOUT_EQUALS}")
    string(APPEND failures "stdout is [${out}], expected [${STDOUT_EQUALS}]\n")
endif()
if(NOT err STREQUAL "${STDERR_EQUALS}")
    string(APPEND failures "stderr is [${err}], expected [${STDERR_EQUALS}]\n")
endif()
if(failures)
    message(FATAL_ERROR "${failures}")
endif()
