set(V4L2_CAPTURE_TEST v4l2_capture_test)

add_executable(${V4L2_CAPTURE_TEST} main.cpp negotiation.cpp)

target_link_libraries(${V4L2_CAPTURE_TEST} PRIVATE
    biopass_face_common
)

target_link_options(${V4L2_CAPTURE_TEST} PRIVATE
    -Wl,--wrap=open,--wrap=close,--wrap=fstat,--wrap=ioctl
)

add_test(NAME v4l2_capture COMMAND ${V4L2_CAPTURE_TEST})
# The test calls into the bundled libcamera.so, which needs libcamera-base.so.
# The executable's RUNPATH only covers its own NEEDED entries, not libcamera's,
# so point the loader at the build-tree copy instead of an installed one.
set_tests_properties(v4l2_capture PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${LIBCAMERA_SODIR}"
)
