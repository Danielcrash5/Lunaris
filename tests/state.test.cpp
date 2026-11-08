#include <gtest/gtest.h>
#include <lunaris/state.h>

#include <lua.h>

#include <type_traits>
#include <utility>

// --- Vertrag zur Compile-Zeit ------------------------------------------------

static_assert(!std::is_copy_constructible_v<lunaris::State>);
static_assert(!std::is_copy_assignable_v<lunaris::State>);
static_assert(std::is_nothrow_move_constructible_v<lunaris::State>);
static_assert(std::is_nothrow_move_assignable_v<lunaris::State>);
static_assert(noexcept(std::declval<const lunaris::State &>().luaState()));

// --- Erzeugen ----------------------------------------------------------------

TEST(StateTest, CreatesStateWithEmptyStack) {
    lunaris::State state;
    ASSERT_NE(state.luaState(), nullptr);
    EXPECT_EQ(lua_gettop(state.luaState()), 0);
}

TEST(StateTest, StatesAreIndependent) {
    lunaris::State first;
    lunaris::State second;
    ASSERT_NE(first.luaState(), second.luaState());

    lua_pushnumber(first.luaState(), 1.0);

    EXPECT_EQ(lua_gettop(first.luaState()), 1);
    EXPECT_EQ(lua_gettop(second.luaState()), 0);
}

// --- Move-Konstruktor --------------------------------------------------------

TEST(StateTest, MoveConstructorTransfersOwnership) {
    lunaris::State source;
    lua_State *raw = source.luaState();

    lunaris::State target(std::move(source));

    EXPECT_EQ(target.luaState(), raw);
    // Dokumentierter Vertrag: Die Quelle ist danach leer.
    EXPECT_EQ(source.luaState(), nullptr); // NOLINT(bugprone-use-after-move)
}

// --- Move-Zuweisung ----------------------------------------------------------

TEST(StateTest, MoveAssignmentTransfersOwnership) {
    lunaris::State source;
    lunaris::State target;
    lua_State *raw = source.luaState();

    target = std::move(source);

    EXPECT_EQ(target.luaState(), raw);
    EXPECT_EQ(source.luaState(), nullptr); // NOLINT(bugprone-use-after-move)
}

// Der alte State des Ziels muss geschlossen werden. Ob das passiert, sieht man
// nur mit dem Sanitizer (LeakSanitizer in der ASan-CI), nicht im Test selbst.
TEST(StateTest, MoveAssignmentReleasesPreviousState) {
    lunaris::State source;
    lunaris::State target;
    lua_State *oldTarget = target.luaState();
    ASSERT_NE(oldTarget, source.luaState());

    target = std::move(source);

    EXPECT_NE(target.luaState(), oldTarget);
}

TEST(StateTest, SelfMoveAssignmentKeepsStateValid) {
    lunaris::State state;
    lua_State *raw = state.luaState();
    lunaris::State &alias = state; // vermeidet die Compiler-Warnung -Wself-move

    state = std::move(alias);

    EXPECT_EQ(state.luaState(), raw);
    EXPECT_EQ(lua_gettop(state.luaState()), 0);
}

// --- Lebensdauer -------------------------------------------------------------

TEST(StateTest, MovedStateSurvivesDestructionOfSource) {
    lunaris::State target;
    {
        lunaris::State source;
        lua_pushnumber(source.luaState(), 42.0);
        target = std::move(source);
    } // Die leere Quelle wird zerstoert und darf target nicht beruehren.

    ASSERT_NE(target.luaState(), nullptr);
    ASSERT_EQ(lua_gettop(target.luaState()), 1);
    EXPECT_EQ(lua_tonumber(target.luaState(), 1), 42.0);
}

TEST(StateTest, MovedFromStateCanBeReassigned) {
    lunaris::State first;
    lunaris::State second(std::move(first));

    first = lunaris::State(); // NOLINT(bugprone-use-after-move)

    ASSERT_NE(first.luaState(), nullptr);
    EXPECT_NE(first.luaState(), second.luaState());
    EXPECT_EQ(lua_gettop(first.luaState()), 0);
}
