foreach(example http_hello taps_get echo_senders)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE stdnet::stdnet)
endforeach()

add_test(NAME example_echo_senders COMMAND echo_senders)
