set(FACE_MATCH_TEST face_match_test)

add_executable(${FACE_MATCH_TEST} main.cpp)

target_include_directories(${FACE_MATCH_TEST} PRIVATE
    ${CMAKE_SOURCE_DIR}/face
)

# biopass_onnx brings the ONNX Runtime and core headers face_recognition.h needs.
target_link_libraries(${FACE_MATCH_TEST} PRIVATE
    biopass_reg
    biopass_onnx
)

add_test(NAME face_match COMMAND ${FACE_MATCH_TEST})
