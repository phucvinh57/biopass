add_executable(keyring_store_test main.cpp)

target_link_libraries(keyring_store_test PRIVATE biopass_keyring)

add_test(NAME keyring_store COMMAND keyring_store_test)
