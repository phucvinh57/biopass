add_executable(keyring_file_test main.cpp)

target_link_libraries(keyring_file_test PRIVATE biopass_keyring)

add_test(NAME keyring_file COMMAND keyring_file_test)
