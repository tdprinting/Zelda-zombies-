cmake_minimum_required(VERSION 3.16)
project(bomap CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# zlib: the system one if there is one (Linux, macOS), otherwise built from source (Windows).
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    include(FetchContent)
    FetchContent_Declare(zlib URL https://zlib.net/fossils/zlib-1.3.1.tar.gz)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    target_include_directories(zlibstatic PUBLIC ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()

add_library(bomap_lib STATIC src/fastfile.cpp src/entities.cpp)
target_include_directories(bomap_lib PUBLIC src)
target_link_libraries(bomap_lib PUBLIC ZLIB::ZLIB)

add_executable(bomap src/main.cpp)
target_link_libraries(bomap PRIVATE bomap_lib)

add_executable(bomap_test tests/bomap_test.cpp)
target_link_libraries(bomap_test PRIVATE bomap_lib)
