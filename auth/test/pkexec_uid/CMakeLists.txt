add_executable(pkexec_uid_test main.cpp)

target_include_directories(pkexec_uid_test PRIVATE
    ${CMAKE_SOURCE_DIR}/keyring
)

add_test(NAME pkexec_uid COMMAND pkexec_uid_test)
