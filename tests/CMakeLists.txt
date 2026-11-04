include(GoogleTest)

add_executable(lunaris_tests
    Unit/StateTests.cpp
    Unit/ValueStackTests.cpp
)

target_link_libraries(lunaris_tests
    PRIVATE
        Lunaris::Lunaris
        GTest::gtest_main
)

gtest_discover_tests(lunaris_tests DISCOVERY_TIMEOUT 30)
