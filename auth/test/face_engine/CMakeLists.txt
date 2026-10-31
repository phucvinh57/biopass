set(FACE_TEST face_engine_test)
add_executable(${FACE_TEST} main.cpp)
target_include_directories(${FACE_TEST} PRIVATE 
    ${ONNXRUNTIME_INCLUDE_DIRS}
    ${FaceDetection_INCLUDE_DIRS}
    ${FaceRecognition_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/face
)
target_link_libraries(${FACE_TEST} PRIVATE
    ${ONNXRUNTIME_LIB}
    biopass_det
    biopass_reg
    biopass_image
    biopass_onnx
    CLI11::CLI11
)

target_compile_definitions(${FACE_TEST} PRIVATE
    BIOPASS_MODEL_DIR="${CMAKE_SOURCE_DIR}/face/models"
)
