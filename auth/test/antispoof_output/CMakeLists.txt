set(ANTISPOOF_OUTPUT_TEST antispoof_output_test)

add_executable(${ANTISPOOF_OUTPUT_TEST} main.cpp)

target_link_libraries(${ANTISPOOF_OUTPUT_TEST} PRIVATE
    biopass_as_output
)

add_test(NAME antispoof_output COMMAND ${ANTISPOOF_OUTPUT_TEST})
