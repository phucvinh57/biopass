set(PAM biopass_pam)
add_library(${PAM} SHARED
    pam.cc
)

# The PAM module only forks biopass-helper; all heavy logic lives there.
target_link_libraries(${PAM}
    pam
)

# Header-only: the helper exit-code contract (core/helper_exit.h).
target_include_directories(${PAM} PRIVATE
    ${CMAKE_SOURCE_DIR}/core
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
    # Linked last, so it ends the RUNPATH after the build-tree entries (a dev
    # build never picks up an installed copy). Needed when the helper is
    # exec'd AT_SECURE (a setuid caller such as su): ld.so then expands an
    # executable's $ORIGIN only into trusted system dirs, and /usr/lib is not
    # one everywhere (Fedora: /lib64, /usr/lib64).
    -Wl,-rpath,/usr/lib/biopass
)

# Shipped as /usr/bin/biopass-helper with its libraries in /usr/lib/biopass
# (tauri.conf.json). BUILD_RPATH entries come first in the RUNPATH.
set_target_properties(biopass-helper PROPERTIES
    BUILD_RPATH "$ORIGIN/../lib/biopass"
)

install(TARGETS biopass-helper RUNTIME DESTINATION "/usr/bin")
