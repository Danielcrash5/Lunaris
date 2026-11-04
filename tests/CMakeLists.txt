include(GoogleTest)

add_executable(lunaris_tests
    Unit/BindingTests.cpp
    Unit/BytecodeTests.cpp
    Unit/BytecodeCacheTests.cpp
    Unit/ErrorTests.cpp
    Unit/GlobalTests.cpp
    Unit/ModuleLoaderTests.cpp
    Unit/ModuleRequireTests.cpp
    Unit/ScriptTests.cpp
    Unit/StateTests.cpp
    Unit/TableTests.cpp
    Unit/ValueStackTests.cpp
)

target_link_libraries(lunaris_tests
    PRIVATE
        Lunaris::Lunaris
        GTest::gtest_main
)

gtest_discover_tests(lunaris_tests DISCOVERY_TIMEOUT 30)
