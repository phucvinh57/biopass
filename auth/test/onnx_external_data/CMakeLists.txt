set(ONNX_EXTERNAL_DATA_TEST onnx_external_data_test)

add_executable(${ONNX_EXTERNAL_DATA_TEST} main.cpp)

target_link_libraries(${ONNX_EXTERNAL_DATA_TEST} PRIVATE
    biopass_onnx
)

add_test(NAME onnx_external_data COMMAND ${ONNX_EXTERNAL_DATA_TEST})
