add_executable(keyring_services_test main.cpp)

target_include_directories(keyring_services_test PRIVATE
    ${CMAKE_SOURCE_DIR}/keyring
)

add_test(NAME keyring_services COMMAND keyring_services_test)
