set(IMAGE_BOUNDS_TEST image_bounds_test)

# detection/utils.cc is plain box maths, built in directly so the test
# doesn't pull ONNX Runtime in through biopass_det.
add_executable(${IMAGE_BOUNDS_TEST}
    main.cpp
    ${CMAKE_SOURCE_DIR}/face/detection/utils.cc
)

target_include_directories(${IMAGE_BOUNDS_TEST} PRIVATE
    ${CMAKE_SOURCE_DIR}/face/detection
)

target_link_libraries(${IMAGE_BOUNDS_TEST} PRIVATE
    biopass_face_common
    biopass_stb
)

add_test(NAME image_bounds COMMAND ${IMAGE_BOUNDS_TEST})
