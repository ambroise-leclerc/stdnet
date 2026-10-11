# Benchmarks (issue #10). Configure with -DSTDNET_BUILD_BENCH=ON; Asio is fetched for comparison.
include(FetchContent)

set(STDNET_ASIO_TAG "asio-1-38-2" CACHE STRING "Asio release used by the benchmarks")
FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG ${STDNET_ASIO_TAG}
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()
add_library(bench_asio INTERFACE)
target_include_directories(bench_asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(bench_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(bench_asio INTERFACE Threads::Threads)

add_executable(loadgen loadgen/loadgen.cpp)
target_compile_features(loadgen PRIVATE cxx_std_20)
target_link_libraries(loadgen PRIVATE Threads::Threads)

function(bench_server name src)
    add_executable(${name} ${src})
    target_compile_features(${name} PRIVATE cxx_std_23)
    target_link_libraries(${name} PRIVATE ${ARGN})
endfunction()

bench_server(srv_raw_blocking servers/raw_blocking.cpp Threads::Threads)
bench_server(srv_raw_epoll servers/raw_epoll.cpp Threads::Threads)
bench_server(srv_asio_callbacks servers/asio_callbacks.cpp bench_asio)
bench_server(srv_asio_coroutines servers/asio_coroutines.cpp bench_asio)
bench_server(srv_stdnet_senders servers/stdnet_senders.cpp stdnet::stdnet)
bench_server(srv_stdnet_coroutines servers/stdnet_coroutines.cpp stdnet::stdnet)

# Instrumented builds (reactor counters) for the diagnostic pass.
bench_server(srv_stdnet_senders_stats servers/stdnet_senders.cpp stdnet::stdnet)
target_compile_definitions(srv_stdnet_senders_stats PRIVATE STDNET_ENABLE_STATS)
bench_server(srv_stdnet_coroutines_stats servers/stdnet_coroutines.cpp stdnet::stdnet)
target_compile_definitions(srv_stdnet_coroutines_stats PRIVATE STDNET_ENABLE_STATS)

# Same programs on the portable poll(2) backend.
bench_server(srv_stdnet_senders_poll servers/stdnet_senders.cpp stdnet::stdnet)
target_compile_definitions(srv_stdnet_senders_poll PRIVATE STDNET_FORCE_POLL)

bench_server(bench_timers inprocess/timers.cpp stdnet::stdnet bench_asio)
bench_server(bench_opstate_sizes inprocess/opstate_sizes.cpp stdnet::stdnet)
