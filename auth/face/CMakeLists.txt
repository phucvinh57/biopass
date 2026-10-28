add_subdirectory(detection)
add_subdirectory(recognition)
add_subdirectory(antispoofing)

# stb image library (JPEG/PNG/BMP/TGA read+write, vendored via FetchContent)
add_library(biopass_stb STATIC
    stb_impl.cc
)
set_target_properties(biopass_stb PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(biopass_stb PUBLIC
    ${STB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Shared ONNX Runtime session plumbing (env/session/allocator/IO names) used
# by the detection, recognition, and anti-spoofing inference engines.
add_library(biopass_onnx STATIC
    common/onnx_session.cc
)
set_target_properties(biopass_onnx PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(biopass_onnx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${ONNXRUNTIME_INCLUDE_DIRS}
)
target_link_libraries(biopass_onnx PUBLIC
    ${ONNXRUNTIME_LIB}
)

# Shared face utilities (camera capture, debug image I/O).
add_library(biopass_face_common STATIC
    common/camera_capture.cc
    common/pixel_convert.cc
    common/debug_image_io.cc
    common/frame_darkness.cc
)
set_target_properties(biopass_face_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(biopass_face_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/core
)
target_link_libraries(biopass_face_common
    biopass_stb
    biopass_core
    libcamera_bundled
    PkgConfig::TURBOJPEG
    spdlog::spdlog_header_only
)
add_dependencies(biopass_face_common libcamera_ext)

# Face auth wrapper library
add_library(biopass_face STATIC
    face_auth.cc
)

set_target_properties(biopass_face PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(biopass_face PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${ONNXRUNTIME_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/detection
    ${CMAKE_CURRENT_SOURCE_DIR}/recognition
    ${CMAKE_CURRENT_SOURCE_DIR}/antispoofing
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(biopass_face
    biopass_core
    biopass_det
    biopass_reg
    biopass_as
    biopass_face_common
    biopass_stb
    ${ONNXRUNTIME_LIB}
)
