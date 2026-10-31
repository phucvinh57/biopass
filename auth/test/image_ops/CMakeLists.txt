set(IMAGE_OPS_TEST image_ops_test)

# detection/utils.cc (scale_boxes) is plain box maths, built in directly so
# the test doesn't pull ONNX Runtime in through biopass_det.
add_executable(${IMAGE_OPS_TEST}
    main.cpp
    ${CMAKE_SOURCE_DIR}/face/detection/utils.cc
)

target_include_directories(${IMAGE_OPS_TEST} PRIVATE
    ${CMAKE_SOURCE_DIR}/face/detection
)

target_link_libraries(${IMAGE_OPS_TEST} PRIVATE
    biopass_image
)

add_test(NAME image_ops COMMAND ${IMAGE_OPS_TEST})
