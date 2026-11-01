add_test(NAME helper_config
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run.sh $<TARGET_FILE:biopass-helper>
)
set_tests_properties(helper_config PROPERTIES SKIP_RETURN_CODE 77)
