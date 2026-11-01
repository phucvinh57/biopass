set(CONFIG_GOLDEN_TEST config_golden_test)

add_executable(${CONFIG_GOLDEN_TEST} main.cpp)

target_link_libraries(${CONFIG_GOLDEN_TEST} PRIVATE
    biopass_core
)

# Written by the app's tests (app/src-tauri/src/config.rs).
target_compile_definitions(${CONFIG_GOLDEN_TEST} PRIVATE
    DEFAULTS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../config/defaults.yaml"
    LIMITS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../config/limits.yaml"
)

add_test(NAME config_golden COMMAND ${CONFIG_GOLDEN_TEST})
