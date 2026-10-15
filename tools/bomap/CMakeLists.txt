cmake_minimum_required(VERSION 3.16)
project(bomap CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# zlib: the system one if there is one (Linux, macOS), otherwise built from source (Windows).
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    include(FetchContent)
    # GitHub's release copy: zlib.net can answer CI runners with a web page instead of the archive.
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    target_include_directories(zlibstatic PUBLIC ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()

add_library(bomap_lib STATIC src/fastfile.cpp src/entities.cpp src/zone_stream.cpp src/zone_walker.cpp src/asset_export.cpp src/texture_source.cpp src/game_export.cpp src/map_light.cpp src/fx_export.cpp src/hud_export.cpp src/player_anims.cpp src/sound_export.cpp)
target_include_directories(bomap_lib PUBLIC src)
target_link_libraries(bomap_lib PUBLIC ZLIB::ZLIB)

add_executable(bomap src/main.cpp)
target_link_libraries(bomap PRIVATE bomap_lib)

add_executable(bomap_test tests/bomap_test.cpp)
target_link_libraries(bomap_test PRIVATE bomap_lib)
