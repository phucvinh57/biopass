# Pure output decoding, separate so unit tests don't need ONNX Runtime.
add_library(biopass_as_output STATIC
    antispoof_output.cc
)
set_target_properties(biopass_as_output PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(biopass_as_output PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(biopass_as SHARED
    face_as.cc
    ir_camera_as.cc
    antispoof_check.cc
)
set(FaceAntiSpoof_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(biopass_as PRIVATE
    ${ONNXRUNTIME_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/face/common
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/face/detection
)
target_include_directories(biopass_as PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(biopass_as PRIVATE
    ${ONNXRUNTIME_LIB}
    biopass_image
    biopass_onnx
    biopass_det
    biopass_core
    biopass_face_common
    biopass_as_output
    spdlog::spdlog_header_only
)
