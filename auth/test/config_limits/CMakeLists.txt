set(CONFIG_LIMITS_TEST config_limits_test)

add_executable(${CONFIG_LIMITS_TEST} main.cpp)

target_link_libraries(${CONFIG_LIMITS_TEST} PRIVATE
    biopass_core
)

add_test(NAME config_limits COMMAND ${CONFIG_LIMITS_TEST})
