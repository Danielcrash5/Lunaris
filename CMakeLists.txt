cmake_minimum_required(VERSION 3.25)

project(Lunaris VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUNARIS_BUILD_TEST ON)
option(LUNARIS_BUILD_EXAMPLES ON)

add_subdirectory("thirdparty/luau")
add_subdirectory("thirdparty/gtest)