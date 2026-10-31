# ==============================================================================
# Bundled libcamera
#
# libcamera has an unstable C++ ABI and bumps its soname (libcamera.so.0.X)
# every minor release, so linking against whatever the build host happens to
# have installed makes the resulting binary fragile across distros/versions.
# Instead we build a pinned libcamera from source and bundle the result into
# the package, the same way ONNX Runtime is already bundled.
#
# Pipelines: uvcvideo for USB/IR webcams, plus simple with its SoftISP IPA for
# MIPI laptop cameras without a hardware ISP (e.g. Intel IPU6, issue #169).
# ==============================================================================

include(ExternalProject)

set(LIBCAMERA_VERSION "v0.7.0")

# libcamera compiles absolute paths into libcamera.so to find its IPA modules,
# IPA proxy workers and IPA tuning files at runtime. Build it for the final
# package layout (must match app/src-tauri/tauri.conf.json) and install into a
# DESTDIR staging tree, so the paths are right once the package is installed:
#   libcamera.so.X.Y   /usr/lib/biopass/                  (ld.so.conf.d/biopass.conf)
#   IPA modules        /usr/lib/biopass/libcamera/ipa/     (libdir/libcamera/ipa)
#   proxy workers      /usr/lib/biopass/libcamera/         (libexecdir/libcamera)
#   IPA tuning files   /usr/share/biopass/libcamera/ipa/   (datadir/libcamera/ipa)
#   configuration      /etc/biopass/libcamera/             (sysconfdir/libcamera)
# Private data/sysconf dirs keep a system libcamera of another version from
# feeding us its tuning or configuration files.
set(LIBCAMERA_PREFIX     "/usr")
set(LIBCAMERA_LIBDIR     "lib/biopass")
set(LIBCAMERA_DATADIR    "share/biopass")
set(LIBCAMERA_SYSCONFDIR "/etc/biopass")

set(LIBCAMERA_STAGING "${CMAKE_BINARY_DIR}/libcamera")
set(LIBCAMERA_ROOT    "${LIBCAMERA_STAGING}${LIBCAMERA_PREFIX}")
set(LIBCAMERA_SODIR   "${LIBCAMERA_ROOT}/${LIBCAMERA_LIBDIR}")
# libcamera installs headers to <prefix>/include/libcamera/libcamera/*.h.
set(LIBCAMERA_INCDIR  "${LIBCAMERA_ROOT}/include/libcamera")
file(MAKE_DIRECTORY "${LIBCAMERA_INCDIR}")

ExternalProject_Add(libcamera_ext
    GIT_REPOSITORY https://git.libcamera.org/libcamera/libcamera.git
    GIT_TAG        ${LIBCAMERA_VERSION}
    GIT_SHALLOW    TRUE
    # Never link gnutls/libcrypto (#176); IPAs are then unsigned and run isolated.
    PATCH_COMMAND  ${CMAKE_COMMAND} -DLIBCAMERA_SOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_LIST_DIR}/PatchLibcameraNoCrypto.cmake
    CONFIGURE_COMMAND meson setup --buildtype=release
        --prefix=${LIBCAMERA_PREFIX} --libdir=${LIBCAMERA_LIBDIR}
        --libexecdir=${LIBCAMERA_LIBDIR} --datadir=${LIBCAMERA_DATADIR}
        --sysconfdir=${LIBCAMERA_SYSCONFDIR}
        -Dpipelines=uvcvideo,simple -Dipas=simple -Dgstreamer=disabled -Dv4l2=disabled
        -Dcam=disabled -Dqcam=disabled -Dlc-compliance=disabled
        -Dtracing=disabled -Ddocumentation=disabled -Dpycamera=disabled -Dtest=false -Dudev=enabled
        <BINARY_DIR> <SOURCE_DIR>
    BUILD_COMMAND   ninja -C <BINARY_DIR>
    INSTALL_COMMAND ${CMAKE_COMMAND} -E env DESTDIR=${LIBCAMERA_STAGING} ninja -C <BINARY_DIR> install
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

# Stage the files Tauri bundles into the package (see tauri.conf.json):
#   libcamera.so.X.Y, libcamera-base.so.X.Y      -> /usr/lib/biopass/
#   libcamera/ipa/ipa_soft_simple.so             -> /usr/lib/biopass/libcamera/ipa/
#   libcamera/soft_ipa_proxy                     -> /usr/lib/biopass/libcamera/
#   ipa/simple/uncalibrated.yaml                 -> /usr/share/biopass/libcamera/ipa/simple/
set(LIBCAMERA_BUNDLE_DIR "${CMAKE_BINARY_DIR}/libcamera-bundle")
set(LIBCAMERA_PRIVDIR    "${LIBCAMERA_SODIR}/libcamera")
set(LIBCAMERA_IPA_DATA   "${LIBCAMERA_ROOT}/${LIBCAMERA_DATADIR}/libcamera/ipa")
ExternalProject_Add_Step(libcamera_ext stage_bundle
    DEPENDEES install
    BYPRODUCTS
        ${LIBCAMERA_BUNDLE_DIR}/libcamera.so.${LIBCAMERA_SOVER}
        ${LIBCAMERA_BUNDLE_DIR}/libcamera-base.so.${LIBCAMERA_SOVER}
        ${LIBCAMERA_BUNDLE_DIR}/libcamera/ipa/ipa_soft_simple.so
        ${LIBCAMERA_BUNDLE_DIR}/libcamera/soft_ipa_proxy
        ${LIBCAMERA_BUNDLE_DIR}/ipa/simple/uncalibrated.yaml
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${LIBCAMERA_BUNDLE_DIR}/libcamera/ipa" "${LIBCAMERA_BUNDLE_DIR}/ipa/simple"
    # `cmake -E copy` follows the staging symlinks, so these land as real files named by soname.
    COMMAND ${CMAKE_COMMAND} -E copy "${LIBCAMERA_SODIR}/libcamera.so.${LIBCAMERA_SOVER}"
            "${LIBCAMERA_BUNDLE_DIR}/libcamera.so.${LIBCAMERA_SOVER}"
    COMMAND ${CMAKE_COMMAND} -E copy "${LIBCAMERA_SODIR}/libcamera-base.so.${LIBCAMERA_SOVER}"
            "${LIBCAMERA_BUNDLE_DIR}/libcamera-base.so.${LIBCAMERA_SOVER}"
    COMMAND ${CMAKE_COMMAND} -E copy "${LIBCAMERA_PRIVDIR}/ipa/ipa_soft_simple.so"
            "${LIBCAMERA_BUNDLE_DIR}/libcamera/ipa/ipa_soft_simple.so"
    COMMAND ${CMAKE_COMMAND} -E copy "${LIBCAMERA_PRIVDIR}/soft_ipa_proxy"
            "${LIBCAMERA_BUNDLE_DIR}/libcamera/soft_ipa_proxy"
    COMMAND ${CMAKE_COMMAND} -E copy "${LIBCAMERA_IPA_DATA}/simple/uncalibrated.yaml"
            "${LIBCAMERA_BUNDLE_DIR}/ipa/simple/uncalibrated.yaml"
)
