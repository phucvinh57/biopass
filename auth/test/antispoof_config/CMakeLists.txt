set(ANTISPOOF_CONFIG_TEST antispoof_config_test)

add_executable(${ANTISPOOF_CONFIG_TEST} main.cpp)

target_link_libraries(${ANTISPOOF_CONFIG_TEST} PRIVATE
    biopass_core
)

add_test(NAME antispoof_config COMMAND ${ANTISPOOF_CONFIG_TEST})
