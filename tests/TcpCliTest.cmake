# Black-box CLI acceptance test, driven by CTest through ${CMAKE_COMMAND} -P.
#
# It runs the built stream1090 binary with no standard input and checks both the
# exact exit status and the diagnostic on stderr. A non-zero result is not
# enough: a crash, a timeout or a startup failure must not pass as a successful
# argument rejection.
#
# Required variable: STREAM1090_EXECUTABLE (set on the add_test command line).

if(NOT DEFINED STREAM1090_EXECUTABLE)
    message(FATAL_ERROR "STREAM1090_EXECUTABLE is not set")
endif()

# Run the binary and require an exact exit code plus a stderr substring.
#   expect_exit(<description> <exit-code> <must-contain> <must-not-contain> <args...>)
# Pass an empty string for <must-not-contain> when there is nothing to exclude.
function(expect_exit description expected_code must_contain must_not_contain)
    execute_process(
        COMMAND "${STREAM1090_EXECUTABLE}" ${ARGN}
        INPUT_FILE /dev/null
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        RESULT_VARIABLE result
        TIMEOUT 20
    )
    if(NOT "${result}" STREQUAL "${expected_code}")
        message(FATAL_ERROR
            "${description}: expected exit ${expected_code}, got '${result}' (stderr: ${err})")
    endif()
    string(FIND "${err}" "${must_contain}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${description}: stderr does not contain '${must_contain}' (stderr: ${err})")
    endif()
    if(NOT "${must_not_contain}" STREQUAL "")
        string(FIND "${err}" "${must_not_contain}" forbidden)
        if(NOT forbidden EQUAL -1)
            message(FATAL_ERROR
                "${description}: stderr unexpectedly contains '${must_not_contain}' (stderr: ${err})")
        endif()
    endif()
endfunction()

# --- --help: exit zero and advertise the new options and examples ------------
execute_process(
    COMMAND "${STREAM1090_EXECUTABLE}" --help
    INPUT_FILE /dev/null
    OUTPUT_VARIABLE help_out
    ERROR_VARIABLE help_err
    RESULT_VARIABLE help_result
    TIMEOUT 20
)
if(NOT "${help_result}" STREQUAL "0")
    message(FATAL_ERROR "--help exited with ${help_result}: ${help_err}")
endif()
foreach(token
        "--net-bind-address"
        "--net-avr-port"
        "--no-stdout"
        "127.0.0.1"
        "raw_in")
    string(FIND "${help_out}" "${token}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "--help does not mention ${token}")
    endif()
endforeach()

# --- Options missing their value --------------------------------------------
expect_exit("--net-avr-port without a value" 1
    "Unknown or incomplete argument" "" -s 2.4 --net-avr-port)
expect_exit("Beast option is not supported" 1
    "Unknown or incomplete argument" "" -s 2.4 --net-beast-port 30007)
expect_exit("--net-bind-address without a value" 1
    "Unknown or incomplete argument" "" -s 2.4 --net-bind-address)

# --- Invalid AVR port values -------------------------------------------------
expect_exit("AVR port 0" 1 "Invalid AVR TCP port" "" -s 2.4 --net-avr-port 0)
expect_exit("AVR port -1" 1 "Invalid AVR TCP port" "" -s 2.4 --net-avr-port -1)
expect_exit("AVR port +1" 1 "Invalid AVR TCP port" "" -s 2.4 --net-avr-port +1)
expect_exit("AVR port 65536" 1 "Invalid AVR TCP port" "" -s 2.4 --net-avr-port 65536)
expect_exit("AVR port overflow" 1 "Invalid AVR TCP port" ""
    -s 2.4 --net-avr-port 999999999999999999999999)
expect_exit("AVR port 1x" 1 "Invalid AVR TCP port" "" -s 2.4 --net-avr-port 1x)
expect_exit("AVR port 1.5" 1 "Invalid AVR TCP port" "" -s 2.4 --net-avr-port 1.5)
expect_exit("AVR port 0x10" 1 "Invalid AVR TCP port" "" -s 2.4 --net-avr-port 0x10)
expect_exit("AVR port with leading space" 1 "Invalid AVR TCP port" "" -s 2.4 --net-avr-port " 123")
expect_exit("AVR port with trailing space" 1 "Invalid AVR TCP port" "" -s 2.4 --net-avr-port "123 ")

# Empty argument values: passed inline because CMake lists drop empty elements.
execute_process(
    COMMAND "${STREAM1090_EXECUTABLE}" -s 2.4 --net-avr-port ""
    INPUT_FILE /dev/null OUTPUT_VARIABLE empty_out ERROR_VARIABLE empty_err
    RESULT_VARIABLE empty_result TIMEOUT 20
)
if(NOT "${empty_result}" STREQUAL "1")
    message(FATAL_ERROR "an empty AVR port value gave exit '${empty_result}' (stderr: ${empty_err})")
endif()
string(FIND "${empty_err}" "Invalid AVR TCP port" empty_found)
if(empty_found EQUAL -1)
    message(FATAL_ERROR "empty AVR port did not report the port diagnostic: ${empty_err}")
endif()

execute_process(
    COMMAND "${STREAM1090_EXECUTABLE}" -s 2.4 --net-bind-address ""
    INPUT_FILE /dev/null OUTPUT_VARIABLE bind_out ERROR_VARIABLE bind_err
    RESULT_VARIABLE bind_result TIMEOUT 20
)
if(NOT "${bind_result}" STREQUAL "1")
    message(FATAL_ERROR "an empty bind address gave exit '${bind_result}' (stderr: ${bind_err})")
endif()
string(FIND "${bind_err}" "TCP bind address must not be empty" bind_found)
if(bind_found EQUAL -1)
    message(FATAL_ERROR "empty bind address did not report the bind diagnostic: ${bind_err}")
endif()

# --- Semantic validation -----------------------------------------------------
expect_exit("--no-stdout without any TCP listener" 1
    "--no-stdout requires" "" -s 2.4 --no-stdout)
expect_exit("--net-bind-address without any listener" 1
    "--net-bind-address has no effect" "" -s 2.4 --net-bind-address 0.0.0.0)

# The bad network option must be reported before the INI file is read, so a
# missing configuration path must not surface its own error instead.
expect_exit("bad AVR port before missing INI" 1
    "Invalid AVR TCP port" "Cannot load device config"
    -s 2.4 -d /un/percorso/inesistente.ini --net-avr-port 0)
expect_exit("--no-stdout before missing INI" 1
    "--no-stdout requires" "Cannot load device config"
    -s 2.4 -d /un/percorso/inesistente.ini --no-stdout)

# --- Cases that need a socket are not checked here ---------------------------
# A successful startup and a failed bind would need a port, and a port this
# script picks is either assumed free or assumed taken -- neither holds on a
# machine that happens to disagree. Both live at the library level instead, in
# tests/TcpOutputServerTest.cpp, which takes its ports from the kernel.
# Everything above needs nothing but the binary and stays here.

message(STATUS "tcp_cli_test: all cases behaved as expected")
