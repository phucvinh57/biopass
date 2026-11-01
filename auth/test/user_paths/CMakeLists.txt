set(USER_PATHS_TEST user_paths_test)

add_executable(${USER_PATHS_TEST} main.cpp)

target_link_libraries(${USER_PATHS_TEST} PRIVATE
    biopass_core
)

add_test(NAME user_paths COMMAND ${USER_PATHS_TEST})
