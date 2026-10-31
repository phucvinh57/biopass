set(FS_USER_TEST fs_user_test)

add_executable(${FS_USER_TEST} main.cpp)

target_link_libraries(${FS_USER_TEST} PRIVATE
    biopass_core
)

add_test(NAME fs_user COMMAND ${FS_USER_TEST})
set_tests_properties(fs_user PROPERTIES SKIP_RETURN_CODE 77)
