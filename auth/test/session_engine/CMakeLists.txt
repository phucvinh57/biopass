set(SESSION_ENGINE_TEST session_engine_test)

add_executable(${SESSION_ENGINE_TEST} main.cpp)

target_include_directories(${SESSION_ENGINE_TEST} PRIVATE
    ${FaceDetection_INCLUDE_DIRS}
    ${FaceAntiSpoof_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/face
    ${CMAKE_SOURCE_DIR}/face/common
)

# biopass_onnx brings the ONNX Runtime and core headers face_as.h needs.
target_link_libraries(${SESSION_ENGINE_TEST} PRIVATE
    biopass_as
    biopass_onnx
    biopass_core
)

add_test(NAME session_engine COMMAND ${SESSION_ENGINE_TEST})
# biopass_as pulls in the bundled libcamera.so, which needs libcamera-base.so
# (see test/capture_policy).
set_tests_properties(session_engine PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${LIBCAMERA_SODIR}"
)
