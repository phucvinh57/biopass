cmake_minimum_required(VERSION 3.22)

add_library(biopass_core STATIC
    auth_manager.cc
    auth_config.cc
    fs_user.cc
    user_file.cc
    user_paths.cc
    model_registry.cc
)

set_target_properties(biopass_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(biopass_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(biopass_core
    pthread
    yaml-cpp::yaml-cpp
    spdlog::spdlog_header_only
    sqlite3
)
