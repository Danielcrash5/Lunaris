#include <gtest/gtest.h>
#include <lunaris/detail/state_context.h>
#include <lunaris/state.h>

#include <memory>
#include <utility>

// These tests observe the context of a State. The context is what lets objects
// that outlive a State (results, handles) find out that the State is gone.

namespace {
using lunaris::State;
using lunaris::detail::StateContext;
} // namespace

// --- Context of a living State -----------------------------------------------

TEST(StateContextTest, LivingStateHasOpenContext) {
    State state;

    const std::shared_ptr<StateContext> context = state.context();

    ASSERT_NE(context, nullptr);
    EXPECT_TRUE(context->isOpen());
    EXPECT_EQ(context->luaState(), state.luaState());
}

TEST(StateContextTest, EachStateHasItsOwnContext) {
    State first;
    State second;

    EXPECT_NE(first.context(), second.context());
    EXPECT_NE(first.luaState(), second.luaState());
}

// --- End of the State
// ----------------------------------------------------------

// Holding the context must not keep the Luau state alive.
TEST(StateContextTest, ContextIsClosedWhenStateIsDestroyed) {
    std::shared_ptr<StateContext> context;
    {
        State state;
        context = state.context();
        ASSERT_TRUE(context->isOpen());
    }

    EXPECT_FALSE(context->isOpen());
    EXPECT_EQ(context->luaState(), nullptr);
}

// --- Move constructor
// --------------------------------------------------------------

TEST(StateContextTest, MoveConstructorKeepsTheContext) {
    State source;
    const std::shared_ptr<StateContext> context = source.context();

    State target(std::move(source));

    EXPECT_EQ(target.context(), context);
    EXPECT_TRUE(context->isOpen());
    EXPECT_EQ(source.context(), nullptr); // NOLINT(bugprone-use-after-move)
}

// --- Move assignment
// ---------------------------------------------------------------

TEST(StateContextTest, MoveAssignmentClosesThePreviousContext) {
    State source;
    State target;
    const std::shared_ptr<StateContext> previous = target.context();
    const std::shared_ptr<StateContext> incoming = source.context();

    target = std::move(source);

    EXPECT_FALSE(previous->isOpen());
    EXPECT_EQ(previous->luaState(), nullptr);
    EXPECT_EQ(target.context(), incoming);
    EXPECT_TRUE(incoming->isOpen());
    EXPECT_EQ(source.context(), nullptr); // NOLINT(bugprone-use-after-move)
}

TEST(StateContextTest, SelfMoveAssignmentKeepsTheContextOpen) {
    State state;
    const std::shared_ptr<StateContext> context = state.context();
    State &alias = state; // avoids the compiler warning -Wself-move

    state = std::move(alias);

    EXPECT_EQ(state.context(), context);
    EXPECT_TRUE(context->isOpen());
}

// --- Moved-from State
// ------------------------------------------------------------------

TEST(StateContextTest, MovedFromStateHasNoContext) {
    State source;
    State target(std::move(source));

    EXPECT_EQ(source.context(), nullptr);  // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(source.luaState(), nullptr); // NOLINT(bugprone-use-after-move)
}

// Destroying a moved-from State must not close the context it gave away.
TEST(StateContextTest, DestroyingMovedFromStateDoesNotCloseTheContext) {
    State target;
    std::shared_ptr<StateContext> context;
    {
        State source;
        context = source.context();
        target = std::move(source);
    }

    EXPECT_TRUE(context->isOpen());
    EXPECT_EQ(target.context(), context);
}

TEST(StateContextTest, MovedFromStateCanReceiveANewContext) {
    State first;
    State second(std::move(first));

    first = State(); // NOLINT(bugprone-use-after-move)

    ASSERT_NE(first.context(), nullptr);
    EXPECT_TRUE(first.context()->isOpen());
    EXPECT_NE(first.context(), second.context());
}
