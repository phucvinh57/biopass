# Needs the swtpm TCTI (libtss2-dev ships it) and, at run time, swtpm itself;
# skipped when either is missing.
pkg_check_modules(TSS2_SWTPM IMPORTED_TARGET tss2-tcti-swtpm)
if(TSS2_SWTPM_FOUND)
    add_executable(keyring_tpm_test main.cpp)
    target_link_libraries(keyring_tpm_test PRIVATE biopass_keyring PkgConfig::TSS2_SWTPM)
    add_test(NAME keyring_tpm COMMAND keyring_tpm_test)
    set_tests_properties(keyring_tpm PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
