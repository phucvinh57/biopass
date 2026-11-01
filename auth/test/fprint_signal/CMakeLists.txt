set(FPRINT_SIGNAL_TEST fprint_signal_test)

add_executable(${FPRINT_SIGNAL_TEST} main.cpp)

target_link_libraries(${FPRINT_SIGNAL_TEST} PRIVATE
    biopass_fingerprint
)

add_test(NAME fprint_signal COMMAND ${FPRINT_SIGNAL_TEST})
