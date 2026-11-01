set(HELPER_EXIT_TEST helper_exit_test)

add_executable(${HELPER_EXIT_TEST} main.cpp)

target_include_directories(${HELPER_EXIT_TEST} PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/pam
)

add_test(NAME helper_exit COMMAND ${HELPER_EXIT_TEST})
