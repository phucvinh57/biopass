set(IMAGE_BOUNDS_TEST image_bounds_test)

add_executable(${IMAGE_BOUNDS_TEST} main.cpp)

target_link_libraries(${IMAGE_BOUNDS_TEST} PRIVATE
    biopass_face_common
    biopass_stb
)

add_test(NAME image_bounds COMMAND ${IMAGE_BOUNDS_TEST})
