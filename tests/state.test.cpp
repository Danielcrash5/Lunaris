#include <gtest/gtest.h>
#include <lunaris/state.h>

TEST(StateTest, CreateAndDestroy) {
    EXPECT_NO_THROW({ lunaris::State state; });
}

TEST(StateTest, MoveConstructor) {
    lunaris::State state1;
    lunaris::State state2(std::move(state1));
    EXPECT_NE(state2.luaState(), nullptr);
}

TEST(StateTest, MoveAssignment) {
    lunaris::State state1;
    lunaris::State state2;
    state2 = std::move(state1);
    EXPECT_NE(state2.luaState(), nullptr);
}
