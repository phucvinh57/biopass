# Sealed login password for keyring unlock (docs/Keyring.md). Linked only by
# biopass-helper and biopass-keyring: libbiopass_pam.so (loaded into gdm, sudo,
# ...) and the Tauri app never pull in tpm2-tss or libcrypt.
add_library(biopass_keyring STATIC
    sealed_file.cc
    sealed_store.cc
    shadow_check.cc
    tpm_seal.cc
)

set_target_properties(biopass_keyring PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(biopass_keyring PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(biopass_keyring PUBLIC
    PkgConfig::TSS2
    PkgConfig::LIBCRYPT
)

# Run as root through pkexec (com.ticklab.biopass.policy). Shipped as
# /usr/libexec/biopass/biopass-keyring (tauri.conf.json).
add_executable(biopass-keyring
    keyring_tool.cc
)

target_link_libraries(biopass-keyring
    biopass_keyring
)

install(TARGETS biopass-keyring RUNTIME DESTINATION "/usr/libexec/biopass")
