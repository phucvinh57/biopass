# Patch step for the bundled libcamera (see BundleLibcamera.cmake).
#
# libcamera's src/libcamera/meson.build auto-detects gnutls, then libcrypto,
# purely to verify IPA module signatures. There is no meson option to turn this
# off, and whichever crypto lib the build host has gets linked; libcrypto.so.3
# then breaks the module on OpenSSL 4 systems (issue #176). Replace both
# lookups with a not-found dependency so libcamera never links a crypto
# library. Without one, signatures can never verify and every IPA runs
# isolated in a proxy worker process.
#
# src/meson.build also signs IPA modules whenever the `openssl` program is on
# the build host. With no crypto library that is pointless and makes libcamera
# log "Public key not valid" on every start, so drop that lookup too.
#
# Usage: cmake -DLIBCAMERA_SOURCE_DIR=<dir> -P PatchLibcameraNoCrypto.cmake
# Idempotent; fails loudly if a libcamera bump changes the patched lines.

set(_marker "# biopass: crypto disabled")

function(_patch_lines _rel_file)
    set(_file "${LIBCAMERA_SOURCE_DIR}/${_rel_file}")
    file(READ "${_file}" _content)
    if(_content MATCHES "${_marker}")
        return()
    endif()
    # Remaining arguments come in (old, new) pairs.
    set(_pairs ${ARGN})
    list(LENGTH _pairs _len)
    math(EXPR _last "${_len} - 1")
    foreach(_i RANGE 0 ${_last} 2)
        math(EXPR _j "${_i} + 1")
        list(GET _pairs ${_i} _old)
        list(GET _pairs ${_j} _new)
        string(FIND "${_content}" "${_old}" _pos)
        if(_pos EQUAL -1)
            message(FATAL_ERROR "libcamera patch: '${_old}' not found in ${_file}; "
                                "update auth/PatchLibcameraNoCrypto.cmake for the new libcamera version")
        endif()
        string(REPLACE "${_old}" "${_new}  ${_marker}" _content "${_content}")
    endforeach()
    file(WRITE "${_file}" "${_content}")
endfunction()

_patch_lines(src/libcamera/meson.build
    "dependency('gnutls', required : false)"    "dependency('', required : false)"
    "dependency('libcrypto', required : false)" "dependency('', required : false)")
_patch_lines(src/meson.build
    "find_program('openssl', required : false)" "dependency('', required : false)")

message(STATUS "libcamera patch: disabled gnutls/libcrypto lookup and IPA signing")
