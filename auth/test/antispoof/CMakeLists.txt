set(ANTISPOOF_TEST antispoof_test)
add_executable(${ANTISPOOF_TEST} main.cpp)
target_include_directories(${ANTISPOOF_TEST} PRIVATE
    ${ONNXRUNTIME_INCLUDE_DIRS}
    ${FaceDetection_INCLUDE_DIRS}
    ${FaceAntiSpoof_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/face
    ${CMAKE_SOURCE_DIR}/face/common
)
target_link_libraries(${ANTISPOOF_TEST} PRIVATE
    ${ONNXRUNTIME_LIB}
    biopass_det
    biopass_as
    biopass_stb
    biopass_onnx
    CLI11::CLI11
)

target_compile_definitions(${ANTISPOOF_TEST} PRIVATE
    BIOPASS_MODEL_DIR="${CMAKE_SOURCE_DIR}/face/models"
)
