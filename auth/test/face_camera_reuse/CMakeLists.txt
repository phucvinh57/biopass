set(FACE_CAMERA_REUSE_TEST face_camera_reuse_test)

add_executable(${FACE_CAMERA_REUSE_TEST} main.cpp)

target_link_libraries(${FACE_CAMERA_REUSE_TEST} PRIVATE
    biopass_face
    biopass_onnx
)

add_test(NAME face_camera_reuse COMMAND ${FACE_CAMERA_REUSE_TEST})
# biopass_face pulls in the bundled libcamera.so, which needs
# libcamera-base.so (see test/capture_policy).
set_tests_properties(face_camera_reuse PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${LIBCAMERA_SODIR}"
)
