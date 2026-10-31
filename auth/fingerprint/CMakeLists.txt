cmake_minimum_required(VERSION 3.22)

add_library(biopass_fingerprint STATIC
    fingerprint_auth.cc
    fingerprint_ffi.cc
    fprint_signal.cc
)

set_target_properties(biopass_fingerprint PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(biopass_fingerprint PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(biopass_fingerprint
    biopass_core
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0 gio-unix-2.0)

target_include_directories(biopass_fingerprint PRIVATE ${GLIB_INCLUDE_DIRS})
target_link_libraries(biopass_fingerprint ${GLIB_LIBRARIES})
