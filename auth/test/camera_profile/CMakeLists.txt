set(CAMERA_PROFILE_TEST camera_profile_test)

add_executable(${CAMERA_PROFILE_TEST} main.cpp)

target_link_libraries(${CAMERA_PROFILE_TEST} PRIVATE
    biopass_core
)

add_test(NAME camera_profile COMMAND ${CAMERA_PROFILE_TEST})
