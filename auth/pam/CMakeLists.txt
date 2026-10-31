set(PAM biopass_pam)
add_library(${PAM} SHARED
    pam.cc
)

# The PAM module only forks biopass-helper; all heavy logic lives there.
target_link_libraries(${PAM}
    pam
)

add_executable(biopass-helper
    helper.cc
)

target_compile_definitions(biopass-helper PRIVATE
    BIOPASS_VERSION="${PROJECT_VERSION}"
)

target_include_directories(biopass-helper PRIVATE
    ${ONNXRUNTIME_INCLUDE_DIRS}
    ${FaceDetection_INCLUDE_DIRS}
    ${FaceAntiSpoof_INCLUDE_DIRS}
    ${FaceRecognition_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/face
    ${CMAKE_SOURCE_DIR}/fingerprint
)

target_link_libraries(biopass-helper
    ${ONNXRUNTIME_LIB}
    biopass_core
    biopass_face
    biopass_fingerprint
    spdlog::spdlog_header_only
)

install(TARGETS biopass-helper RUNTIME DESTINATION "/usr/bin")
