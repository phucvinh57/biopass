set(FRAME_DARKNESS_TEST frame_darkness_test)

add_executable(${FRAME_DARKNESS_TEST} main.cpp)

target_link_libraries(${FRAME_DARKNESS_TEST} PRIVATE
    biopass_face_common
    biopass_stb
)

add_test(NAME frame_darkness COMMAND ${FRAME_DARKNESS_TEST})
