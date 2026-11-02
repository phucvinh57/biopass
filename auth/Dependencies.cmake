# ONNX Runtime
set(ONNXRUNTIME_VERSION "1.31.1")
include(FetchContent)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(ONNXRUNTIME_ARCH "linux-aarch64")
else()
    set(ONNXRUNTIME_ARCH "linux-x64")
endif()

FetchContent_Declare(
    onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-${ONNXRUNTIME_ARCH}-${ONNXRUNTIME_VERSION}.tgz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(onnxruntime)

set(ONNXRUNTIME_ROOT "${onnxruntime_SOURCE_DIR}")
set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT}/include")
set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_ROOT}/lib")
find_library(ONNXRUNTIME_LIB onnxruntime PATHS ${ONNXRUNTIME_LIB_DIR} NO_DEFAULT_PATH)

# libturbojpeg: system package (stable soname)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)

# libcamera: built from source and bundled; provides `libcamera_bundled`.
include(${CMAKE_CURRENT_LIST_DIR}/BundleLibcamera.cmake)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        d14d46f6b44456eb88beac58cba833a4135d4c89
)
FetchContent_MakeAvailable(stb)
set(STB_INCLUDE_DIRS "${stb_SOURCE_DIR}")

# yaml-cpp
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        yaml-cpp-0.9.0
)
FetchContent_MakeAvailable(yaml-cpp)

# CLI11
find_package(CLI11 REQUIRED)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

# sqlite3 amalgamation, pinned by hash: read by the PAM helper (biopass.db).
# SHA3-256 as published on https://www.sqlite.org/download.html.
set(SQLITE3_VERSION "3540100")
FetchContent_Declare(
    sqlite3_amalgamation
    URL https://www.sqlite.org/2026/sqlite-amalgamation-${SQLITE3_VERSION}.zip
    URL_HASH SHA3_256=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3_amalgamation)

add_library(sqlite3 STATIC ${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite3_amalgamation_SOURCE_DIR})
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0
)

# tpm2-tss and libxcrypt: system packages, used by biopass-helper and
# biopass-keyring to seal the login password for keyring unlock
# (docs/Keyring.md). The device TCTI is linked directly, not through
# tss2-tctildr, so no TCTI is dlopen'ed at runtime.
pkg_check_modules(TSS2 REQUIRED IMPORTED_TARGET tss2-esys tss2-mu tss2-tcti-device)
pkg_check_modules(LIBCRYPT REQUIRED IMPORTED_TARGET libcrypt)
