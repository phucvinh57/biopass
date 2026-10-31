# Patch step for the bundled libcamera (see BundleLibcamera.cmake).
#
# libcamera's src/libcamera/meson.build auto-detects gnutls, then libcrypto,
# purely to verify IPA module signatures. There is no meson option to turn this
# off. We build with -Dipas=[] so there is nothing to verify, yet whichever
# crypto lib the build host has gets linked anyway; libcrypto.so.3 then breaks
# the module on OpenSSL 4 systems (issue #176). Replace both lookups with a
# not-found dependency so libcamera never links a crypto library.
#
# Usage: cmake -DLIBCAMERA_SOURCE_DIR=<dir> -P PatchLibcameraNoCrypto.cmake
# Idempotent; fails loudly if a libcamera bump changes the patched lines.

set(_file "${LIBCAMERA_SOURCE_DIR}/src/libcamera/meson.build")
file(READ "${_file}" _content)

set(_marker "# biopass: crypto disabled")
if(_content MATCHES "${_marker}")
    return()
endif()

foreach(_dep gnutls libcrypto)
    set(_old "dependency('${_dep}', required : false)")
    string(FIND "${_content}" "${_old}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "libcamera patch: '${_old}' not found in ${_file}; "
                            "update auth/PatchLibcameraNoCrypto.cmake for the new libcamera version")
    endif()
    string(REPLACE "${_old}" "dependency('', required : false)  ${_marker}" _content "${_content}")
endforeach()

file(WRITE "${_file}" "${_content}")
message(STATUS "libcamera patch: disabled gnutls/libcrypto lookup")
