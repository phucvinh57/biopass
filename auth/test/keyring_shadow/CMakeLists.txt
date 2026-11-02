add_executable(keyring_shadow_test main.cpp)

target_link_libraries(keyring_shadow_test PRIVATE biopass_keyring)

add_test(NAME keyring_shadow COMMAND keyring_shadow_test)
