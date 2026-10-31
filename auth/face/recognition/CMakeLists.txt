add_library(biopass_reg SHARED
    face_recognition.cc
)
set(FaceRecognition_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(biopass_reg PRIVATE
    ${ONNXRUNTIME_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(biopass_reg PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(biopass_reg PRIVATE
    ${ONNXRUNTIME_LIB}
    biopass_image
    biopass_onnx
)
