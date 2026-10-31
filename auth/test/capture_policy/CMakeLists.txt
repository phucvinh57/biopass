set(CAPTURE_POLICY_TEST capture_policy_test)

add_executable(${CAPTURE_POLICY_TEST} main.cpp)

target_link_libraries(${CAPTURE_POLICY_TEST} PRIVATE
    biopass_face_common
)

add_test(NAME capture_policy COMMAND ${CAPTURE_POLICY_TEST})
