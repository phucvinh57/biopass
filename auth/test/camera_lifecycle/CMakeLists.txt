set(CAMERA_LIFECYCLE_TEST camera_lifecycle_test)

add_executable(${CAMERA_LIFECYCLE_TEST} main.cpp)

target_link_libraries(${CAMERA_LIFECYCLE_TEST} PRIVATE
    biopass_face_common
)

add_test(NAME camera_lifecycle COMMAND ${CAMERA_LIFECYCLE_TEST})
