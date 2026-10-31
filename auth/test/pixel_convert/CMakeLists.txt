set(PIXEL_CONVERT_TEST pixel_convert_test)

add_executable(${PIXEL_CONVERT_TEST} main.cpp)

target_link_libraries(${PIXEL_CONVERT_TEST} PRIVATE
    biopass_face_common
)

add_test(NAME pixel_convert COMMAND ${PIXEL_CONVERT_TEST})
