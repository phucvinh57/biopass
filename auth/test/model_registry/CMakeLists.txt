set(MODEL_REGISTRY_TEST model_registry_test)

add_executable(${MODEL_REGISTRY_TEST} main.cpp)

target_link_libraries(${MODEL_REGISTRY_TEST} PRIVATE
    biopass_core
    sqlite3
)

add_test(NAME model_registry COMMAND ${MODEL_REGISTRY_TEST})
set_tests_properties(model_registry PROPERTIES SKIP_RETURN_CODE 77)
