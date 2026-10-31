set(HELPER_WAIT_TEST helper_wait_test)

add_executable(${HELPER_WAIT_TEST} main.cpp)

target_include_directories(${HELPER_WAIT_TEST} PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/pam
)

add_test(NAME helper_wait COMMAND ${HELPER_WAIT_TEST})
# Backstop: a wait that never returns must fail the test, not hang ctest.
set_tests_properties(helper_wait PROPERTIES TIMEOUT 30)
