cmake_minimum_required(VERSION 3.25)

project(Lunaris VERSION 0.1.0 LANGUAGES CXX)

option(LUNARIS_BUILD_TESTS "Build Lunaris tests" ON)
option(LUNARIS_BUILD_EXAMPLES "Build Lunaris examples" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LUAU_BUILD_CLI OFF CACHE BOOL "Build Luau command-line tools" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "Build Luau tests" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "Build Luau web module" FORCE)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/luau/CMakeLists.txt")
    message(FATAL_ERROR "Vendored Luau is missing from thirdparty/luau.")
endif()

add_subdirectory(thirdparty/luau EXCLUDE_FROM_ALL)

add_library(lunaris
    src/State.cpp
    src/Table.cpp
)
add_library(Lunaris::Lunaris ALIAS lunaris)

target_compile_features(lunaris PUBLIC cxx_std_23)
target_include_directories(lunaris
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(lunaris PUBLIC Luau.VM)

if (MSVC)
    target_compile_options(lunaris PRIVATE /W4 /permissive-)
else()
    target_compile_options(lunaris PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (LUNARIS_BUILD_TESTS)
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/googletest/CMakeLists.txt")
        message(FATAL_ERROR "Vendored GoogleTest is missing from thirdparty/googletest.")
    endif()

    set(BUILD_GMOCK OFF CACHE BOOL "Build GoogleMock" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "Install GoogleTest" FORCE)
    enable_testing()
    add_subdirectory(thirdparty/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()

if (LUNARIS_BUILD_EXAMPLES)
    add_executable(lunaris_state_example examples/state.cpp)
    target_link_libraries(lunaris_state_example PRIVATE Lunaris::Lunaris)
endif()
