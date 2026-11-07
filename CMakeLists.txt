cmake_minimum_required(VERSION 3.25)

project(Lunaris VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUNARIS_BUILD_TESTS "Whether Unit tests should be build" ON)
option(LUNARIS_BUILD_EXAMPLES "Whether the example programs should be build" ON)

set(LUAU_BUILD_CLI OFF CACHE BOOL "Build Luau command-line tools" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "Build Luau tests" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "Build Luau web module" FORCE)

add_subdirectory("thirdparty/luau")
add_subdirectory("thirdparty/gtest")