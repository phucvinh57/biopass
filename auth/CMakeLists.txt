cmake_minimum_required(VERSION 3.22)
# Read the version (env in CI, -DPROJECT_VERSION from Makefile) before
# project() clears it.
if(DEFINED ENV{PROJECT_VERSION})
    set(BIOPASS_VERSION $ENV{PROJECT_VERSION})
elseif(PROJECT_VERSION)
    set(BIOPASS_VERSION ${PROJECT_VERSION})
else()
    set(BIOPASS_VERSION 0.1.0)
endif()

project(biopass)
set(PROJECT_VERSION ${BIOPASS_VERSION})
message(STATUS "Project version: ${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

# Avoid RPATH warnings about shadowed libraries in non-system directories
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_SKIP_BUILD_RPATH FALSE)
include(Dependencies.cmake)

set(FaceDetection_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/face/detection)
set(FaceRecognition_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/face/recognition)
set(FaceAntiSpoof_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/face/antispoofing)

add_subdirectory(core)
add_subdirectory(face)
add_subdirectory(fingerprint)
add_subdirectory(pam)

option(BUILD_TESTS "Build test configurations" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test/camera)
    add_subdirectory(test/face_engine)
    add_subdirectory(test/antispoof)
    add_subdirectory(test/antispoof_output)
    add_subdirectory(test/pixel_convert)
    add_subdirectory(test/image_bounds)
    add_subdirectory(test/camera_lifecycle)
    add_subdirectory(test/frame_sampling)
    add_subdirectory(test/frame_darkness)
    add_subdirectory(test/fs_user)
    add_subdirectory(test/helper_config)
    add_subdirectory(test/user_file)
    add_subdirectory(test/model_registry)
    add_subdirectory(test/onnx_external_data)
    add_subdirectory(test/fprint_signal)
    add_subdirectory(test/auth_manager)
    add_subdirectory(test/helper_exit)
    add_subdirectory(test/helper_argv)
    add_subdirectory(test/helper_wait)
    add_subdirectory(test/config_limits)
    add_subdirectory(test/config_validation)
    add_subdirectory(test/user_paths)
    add_subdirectory(test/ir_presence)
endif()

install(
    TARGETS biopass_det biopass_reg biopass_as
    COMPONENT applications
    RUNTIME DESTINATION "/usr/bin"
    LIBRARY DESTINATION "/usr/lib/biopass"
)
install(
    TARGETS biopass_pam
    LIBRARY DESTINATION "/lib/security"
)

install(
    DIRECTORY ${ONNXRUNTIME_LIB_DIR}/
    DESTINATION "/usr/lib/biopass"
    FILES_MATCHING PATTERN "*.so*"
)
