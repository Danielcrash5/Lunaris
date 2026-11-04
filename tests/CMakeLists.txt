include(GoogleTest)

add_executable(lunaris_tests
    Unit/BindingTests.cpp
    Unit/BytecodeTests.cpp
    Unit/BytecodeCacheTests.cpp
    Unit/ErrorTests.cpp
    Unit/FutureTests.cpp
    Unit/GlobalTests.cpp
    Unit/ModuleLoaderTests.cpp
    Unit/ModuleRequireTests.cpp
    Unit/ScriptTests.cpp
    Unit/SharedTableTests.cpp
    Unit/StateTests.cpp
    Unit/TableTests.cpp
    Unit/ValueStackTests.cpp
)

if (LUNARIS_ENABLE_LUAROCKS)
    target_sources(lunaris_tests PRIVATE Unit/LuaRocksProviderTests.cpp)
endif()

target_link_libraries(lunaris_tests
    PRIVATE
        Lunaris::Lunaris
        GTest::gtest_main
)

gtest_discover_tests(lunaris_tests DISCOVERY_TIMEOUT 30)
