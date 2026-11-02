add_test(NAME runpath
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/check.sh
        $<TARGET_FILE:biopass-helper>
        $<TARGET_FILE:biopass-keyring>
        $<TARGET_FILE:biopass_pam>
        $<TARGET_FILE:biopass_det>
        $<TARGET_FILE:biopass_reg>
        $<TARGET_FILE:biopass_as>
        ${ONNXRUNTIME_LIB_DIR}/libonnxruntime.so.${ONNXRUNTIME_VERSION}
        ${LIBCAMERA_BUNDLE_DIR}/libcamera.so.${LIBCAMERA_SOVER}
        ${LIBCAMERA_BUNDLE_DIR}/libcamera-base.so.${LIBCAMERA_SOVER}
        ${LIBCAMERA_BUNDLE_DIR}/libcamera/ipa/ipa_softisp.so
        ${LIBCAMERA_BUNDLE_DIR}/libcamera/softisp_ipa_proxy
)
set_tests_properties(runpath PROPERTIES SKIP_RETURN_CODE 77)
