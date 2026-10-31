set(USER_FILE_TEST user_file_test)

add_executable(${USER_FILE_TEST} main.cpp)

target_link_libraries(${USER_FILE_TEST} PRIVATE
    biopass_core
)

add_test(NAME user_file COMMAND ${USER_FILE_TEST})
