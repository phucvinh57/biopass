set(CAPTURE_POLICY_TEST capture_policy_test)

add_executable(${CAPTURE_POLICY_TEST} main.cpp)

target_link_libraries(${CAPTURE_POLICY_TEST} PRIVATE
    biopass_face_common
)

add_test(NAME capture_policy COMMAND ${CAPTURE_POLICY_TEST})
# The test calls into the bundled libcamera.so, which needs libcamera-base.so.
# The executable's RUNPATH only covers its own NEEDED entries, not libcamera's,
# so point the loader at the build-tree copy instead of an installed one.
set_tests_properties(capture_policy PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${LIBCAMERA_SODIR}"
)
