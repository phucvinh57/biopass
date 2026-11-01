set(IR_PRESENCE_TEST ir_presence_test)

add_executable(${IR_PRESENCE_TEST} main.cpp)

target_include_directories(${IR_PRESENCE_TEST} PRIVATE
    ${CMAKE_SOURCE_DIR}/face
    ${CMAKE_SOURCE_DIR}/face/common
    ${FaceAntiSpoof_INCLUDE_DIRS}
)

target_link_libraries(${IR_PRESENCE_TEST} PRIVATE
    biopass_as
    biopass_image
    biopass_core
)

add_test(NAME ir_presence COMMAND ${IR_PRESENCE_TEST})
