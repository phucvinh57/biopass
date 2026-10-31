set(PIXEL_CONVERT_TEST pixel_convert_test)

add_executable(${PIXEL_CONVERT_TEST} main.cpp)

target_link_libraries(${PIXEL_CONVERT_TEST} PRIVATE
    biopass_face_common
)
