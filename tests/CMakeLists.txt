include(GoogleTest)

add_executable(LunarisTests
    state.test.cpp
    state_context.test.cpp
    libraries.test.cpp
)

target_link_libraries(LunarisTests PRIVATE Lunaris::Lunaris GTest::gtest_main)

gtest_discover_tests(LunarisTests)