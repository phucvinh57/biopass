set(AUTH_MANAGER_TEST auth_manager_test)

add_executable(${AUTH_MANAGER_TEST} main.cpp)

target_link_libraries(${AUTH_MANAGER_TEST} PRIVATE
    biopass_core
)

add_test(NAME auth_manager COMMAND ${AUTH_MANAGER_TEST})
