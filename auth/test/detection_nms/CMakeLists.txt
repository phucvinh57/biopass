set(DETECTION_NMS_TEST detection_nms_test)

# detection/utils.cc is plain box maths, built in directly so the test
# doesn't pull ONNX Runtime in through biopass_det.
add_executable(${DETECTION_NMS_TEST}
    main.cpp
    ${CMAKE_SOURCE_DIR}/face/detection/utils.cc
)

target_include_directories(${DETECTION_NMS_TEST} PRIVATE
    ${CMAKE_SOURCE_DIR}/face/detection
)

add_test(NAME detection_nms COMMAND ${DETECTION_NMS_TEST})
