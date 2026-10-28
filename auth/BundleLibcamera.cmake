# ==============================================================================
# Bundled libcamera
#
# libcamera has an unstable C++ ABI and bumps its soname (libcamera.so.0.X)
# every minor release, so linking against whatever the build host happens to
# have installed makes the resulting binary fragile across distros/versions.
# Instead we build a pinned libcamera from source (minimal uvcvideo-only
# config, since all biopass cameras are UVC/IR USB webcams) and bundle the
# result into the package, the same way ONNX Runtime is already bundled.
# ==============================================================================

include(ExternalProject)

set(LIBCAMERA_VERSION "v0.7.0")
set(LIBCAMERA_STAGING "${CMAKE_BINARY_DIR}/libcamera")
set(LIBCAMERA_SODIR   "${LIBCAMERA_STAGING}/lib")
# libcamera installs headers to <prefix>/include/libcamera/libcamera/*.h.
set(LIBCAMERA_INCDIR  "${LIBCAMERA_STAGING}/include/libcamera")
file(MAKE_DIRECTORY "${LIBCAMERA_INCDIR}")

ExternalProject_Add(libcamera_ext
    GIT_REPOSITORY https://git.libcamera.org/libcamera/libcamera.git
    GIT_TAG        ${LIBCAMERA_VERSION}
    GIT_SHALLOW    TRUE
    # Never link gnutls/libcrypto: only used for IPA signing, and we build no IPAs (#176).
    PATCH_COMMAND  ${CMAKE_COMMAND} -DLIBCAMERA_SOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_LIST_DIR}/PatchLibcameraNoCrypto.cmake
    CONFIGURE_COMMAND meson setup --prefix=${LIBCAMERA_STAGING} --libdir=lib --buildtype=release
        -Dpipelines=uvcvideo -Dipas=[] -Dgstreamer=disabled -Dv4l2=disabled
        -Dcam=disabled -Dqcam=disabled -Dlc-compliance=disabled
        -Dtracing=disabled -Ddocumentation=disabled -Dpycamera=disabled -Dtest=false -Dudev=enabled
        <BINARY_DIR> <SOURCE_DIR>
    BUILD_COMMAND   ninja -C <BINARY_DIR>
    INSTALL_COMMAND ninja -C <BINARY_DIR> install
    BUILD_BYPRODUCTS ${LIBCAMERA_SODIR}/libcamera.so ${LIBCAMERA_SODIR}/libcamera-base.so
)

# Consumers link this; DT_NEEDED records the pinned soname (libcamera.so.0.7).
add_library(libcamera_bundled INTERFACE)
add_dependencies(libcamera_bundled libcamera_ext)
target_include_directories(libcamera_bundled INTERFACE ${LIBCAMERA_INCDIR})
target_link_libraries(libcamera_bundled INTERFACE
    ${LIBCAMERA_SODIR}/libcamera.so ${LIBCAMERA_SODIR}/libcamera-base.so)

# Derive the soname suffix from the pinned version (v0.7.0 -> 0.7).
string(REGEX REPLACE "^v?([0-9]+)\\.([0-9]+).*" "\\1.\\2" LIBCAMERA_SOVER "${LIBCAMERA_VERSION}")

# Stage dereferenced core libs named by soname for Tauri to bundle into the package.
set(LIBCAMERA_BUNDLE_DIR "${CMAKE_BINARY_DIR}/libcamera-bundle")
ExternalProject_Add_Step(libcamera_ext stage_bundle
    DEPENDEES install
    BYPRODUCTS
        ${LIBCAMERA_BUNDLE_DIR}/libcamera.so.${LIBCAMERA_SOVER}
        ${LIBCAMERA_BUNDLE_DIR}/libcamera-base.so.${LIBCAMERA_SOVER}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBCAMERA_BUNDLE_DIR}"
    # `cmake -E copy` follows the staging symlinks, so these land as real files named by soname.
    COMMAND ${CMAKE_COMMAND} -E copy "${LIBCAMERA_SODIR}/libcamera.so.${LIBCAMERA_SOVER}"
            "${LIBCAMERA_BUNDLE_DIR}/libcamera.so.${LIBCAMERA_SOVER}"
    COMMAND ${CMAKE_COMMAND} -E copy "${LIBCAMERA_SODIR}/libcamera-base.so.${LIBCAMERA_SOVER}"
            "${LIBCAMERA_BUNDLE_DIR}/libcamera-base.so.${LIBCAMERA_SOVER}"
)
