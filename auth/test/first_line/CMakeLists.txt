set(FIRST_LINE_TEST first_line_test)

add_executable(${FIRST_LINE_TEST} main.cpp)

target_link_libraries(${FIRST_LINE_TEST} PRIVATE
    biopass_onnx
)

add_test(NAME first_line COMMAND ${FIRST_LINE_TEST})
