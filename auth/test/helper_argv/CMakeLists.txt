set(HELPER_ARGV_TEST helper_argv_test)

add_executable(${HELPER_ARGV_TEST} main.cpp)

target_include_directories(${HELPER_ARGV_TEST} PRIVATE
    ${CMAKE_SOURCE_DIR}/pam
)

add_test(NAME helper_argv COMMAND ${HELPER_ARGV_TEST})
