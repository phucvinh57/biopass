set(FRAME_SAMPLING_TEST frame_sampling_test)

add_executable(${FRAME_SAMPLING_TEST} main.cpp)

target_link_libraries(${FRAME_SAMPLING_TEST} PRIVATE
    biopass_face_common
)

add_test(NAME frame_sampling COMMAND ${FRAME_SAMPLING_TEST})
