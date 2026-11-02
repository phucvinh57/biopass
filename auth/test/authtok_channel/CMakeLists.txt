add_executable(authtok_channel_test main.cpp)

target_include_directories(authtok_channel_test PRIVATE
    ${CMAKE_SOURCE_DIR}/pam
)

add_test(NAME authtok_channel COMMAND authtok_channel_test)
# Backstop: a read that blocks must fail the test, not hang ctest.
set_tests_properties(authtok_channel PROPERTIES TIMEOUT 30)
