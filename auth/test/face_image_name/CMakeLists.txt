set(FACE_IMAGE_NAME_TEST face_image_name_test)

add_executable(${FACE_IMAGE_NAME_TEST} main.cpp)

target_link_libraries(${FACE_IMAGE_NAME_TEST} PRIVATE
    biopass_core
)

target_compile_definitions(${FACE_IMAGE_NAME_TEST} PRIVATE
    CASES_FILE="${CMAKE_CURRENT_SOURCE_DIR}/cases.txt"
)

add_test(NAME face_image_name COMMAND ${FACE_IMAGE_NAME_TEST})
