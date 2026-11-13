cmake_minimum_required(VERSION 3.25)

# Die Version steht nur hier.
project(Lunaris VERSION 0.1.0 LANGUAGES CXX)

# Nur im eigenen Repo: compile_commands.json fuer clang-tidy.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# --- Optionen ----------------------------------------------------------------
# Als Unterprojekt eines anderen Projekts werden Tests und Examples nicht gebaut.
option(LUNARIS_BUILD_TESTS "Unit-Tests bauen" ${PROJECT_IS_TOP_LEVEL})
option(LUNARIS_BUILD_EXAMPLES "Examples bauen" ${PROJECT_IS_TOP_LEVEL})

# --- Luau ---------------------------------------------------------------------
# Luau ruft intern cmake_minimum_required(3.10) auf. Normale Variablen werden
# dort von option() ignoriert, deshalb CACHE ... FORCE.
# Hat das umgebende Projekt schon Luau eingebunden, wird dessen Luau verwendet.
if(NOT TARGET Luau.VM)
    set(LUAU_BUILD_CLI OFF CACHE BOOL "Build Luau command-line tools" FORCE)
    set(LUAU_BUILD_TESTS OFF CACHE BOOL "Build Luau tests" FORCE)
    set(LUAU_BUILD_WEB OFF CACHE BOOL "Build Luau web module" FORCE)
    # Muss aus bleiben: Exceptions ueber Luau-Frames brauchen die C++-Variante.
    set(LUAU_EXTERN_C OFF CACHE BOOL "Use extern C for all APIs" FORCE)

    block()
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        # EXCLUDE_FROM_ALL: gebaut wird nur, was Lunaris wirklich verlinkt
        # (VM und Compiler), nicht Analysis, CodeGen, Require usw.
        add_subdirectory("thirdparty/luau" EXCLUDE_FROM_ALL)
    endblock()
endif()

# --- Bibliothek ---------------------------------------------------------------
# Quelldateien explizit aufzaehlen, kein file(GLOB).
add_library(Lunaris STATIC
    src/state.cpp
    src/state_context.cpp
    src/error.cpp
)
add_library(Lunaris::Lunaris ALIAS Lunaris)

target_include_directories(Lunaris
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# PUBLIC, weil die Lunaris-Header die Luau-Header einbinden.
target_link_libraries(Lunaris
    PUBLIC
        Luau.VM
        Luau.Compiler
)

# Der Standard wird an Nutzer weitergegeben, die Extensions nicht.
target_compile_features(Lunaris PUBLIC cxx_std_23)
set_target_properties(Lunaris PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

target_compile_options(Lunaris PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# --- Tests und Examples -----------------------------------------------------
# enable_testing() muss in der Wurzel stehen. Examples laufen ebenfalls als Tests.
if(LUNARIS_BUILD_TESTS OR LUNARIS_BUILD_EXAMPLES)
    enable_testing()
endif()

if(LUNARIS_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory("thirdparty/googletest" EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()

if(LUNARIS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
