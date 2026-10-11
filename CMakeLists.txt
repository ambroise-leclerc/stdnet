cmake_minimum_required(VERSION 3.25)
project(stdnet VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "A prospective C++29 <network> header built on std::execution")

set(STDNET_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})
option(STDNET_BUILD_TESTS "Build stdnet tests" ${STDNET_IS_TOP_LEVEL})
option(STDNET_BUILD_EXAMPLES "Build stdnet examples" ${STDNET_IS_TOP_LEVEL})
option(STDNET_USE_STDEXEC "Use NVIDIA stdexec even if the standard library provides std::execution" OFF)
set(STDNET_STDEXEC_TAG "7df6de8eeec667c8adce66fa5a3e17cfd6849e48" CACHE STRING "stdexec commit to fetch")

add_library(stdnet INTERFACE)
add_library(stdnet::stdnet ALIAS stdnet)
target_include_directories(stdnet INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(stdnet INTERFACE cxx_std_23)

find_package(Threads REQUIRED)
target_link_libraries(stdnet INTERFACE Threads::Threads)
if(WIN32)
    target_link_libraries(stdnet INTERFACE ws2_32)
endif()

# std::execution: use the standard library's when it has senders (__cpp_lib_senders), else stdexec.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX20_STANDARD_COMPILE_OPTION}")
set(CMAKE_REQUIRED_DEFINITIONS)
check_cxx_source_compiles("
#include <version>
#if !defined(__cpp_lib_senders)
#error no senders
#endif
int main() {}" STDNET_HAS_STD_SENDERS)

if(STDNET_USE_STDEXEC OR NOT STDNET_HAS_STD_SENDERS)
    if(NOT TARGET STDEXEC::stdexec)
        include(FetchContent)
        set(STDEXEC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(STDEXEC_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(STDEXEC_ENABLE_CUDA OFF CACHE BOOL "" FORCE)
        set(STDEXEC_ENABLE_TBB OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(stdexec
            GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
            GIT_TAG ${STDNET_STDEXEC_TAG}
            GIT_SHALLOW FALSE)
        FetchContent_GetProperties(stdexec)
        if(NOT stdexec_POPULATED)
            FetchContent_Populate(stdexec)
        endif()
        add_library(stdnet_stdexec INTERFACE)
        target_include_directories(stdnet_stdexec SYSTEM INTERFACE ${stdexec_SOURCE_DIR}/include)
        target_compile_features(stdnet_stdexec INTERFACE cxx_std_20)
        add_library(STDEXEC::stdexec ALIAS stdnet_stdexec)
    endif()
    target_link_libraries(stdnet INTERFACE STDEXEC::stdexec)
    target_compile_definitions(stdnet INTERFACE STDNET_USE_STDEXEC=1)
endif()

if(STDNET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(STDNET_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

install(DIRECTORY include/ DESTINATION include)
