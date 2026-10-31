set(CONFIG_VALIDATION_TEST config_validation_test)

add_executable(${CONFIG_VALIDATION_TEST} main.cpp)

target_link_libraries(${CONFIG_VALIDATION_TEST} PRIVATE
    biopass_core
)

add_test(NAME config_validation COMMAND ${CONFIG_VALIDATION_TEST})
