#include "lunaris/State.h"

#include <gtest/gtest.h>
#include <lua.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

TEST(TableTests, ReadsAndWritesScalarFieldsWithoutChangingStack) {
    lunaris::State state;
    lunaris::Table table = state.create_table();
    const int initialTop = state.top();

    table.set("integer", lunaris::Value{std::int64_t{42}});
    table.set("boolean", lunaris::Value{true});
    table.set("number", lunaris::Value{3.5});
    table.set("string", lunaris::Value{std::string("value\0bytes", 11)});

    EXPECT_EQ(std::get<std::int64_t>(table.get("integer")), 42);
    EXPECT_TRUE(std::get<bool>(table.get("boolean")));
    EXPECT_DOUBLE_EQ(std::get<double>(table.get("number")), 3.5);
    EXPECT_EQ(std::get<std::string>(table.get("string")),
              std::string("value\0bytes", 11));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(table.get("missing")));
    EXPECT_EQ(state.top(), initialTop);
}

TEST(TableTests, NilRemovesField) {
    lunaris::State state;
    lunaris::Table table = state.create_table();

    table.set("field", lunaris::Value{std::int64_t{7}});
    table.set("field", lunaris::Value{std::monostate{}});

    EXPECT_TRUE(std::holds_alternative<std::monostate>(table.get("field")));
}

TEST(TableTests, RegistryHandleKeepsTableAliveAfterStackAndCollection) {
    lunaris::State state;
    lunaris::Table table = state.create_table();

    table.set("field", lunaris::Value{std::int64_t{91}});
    lua_gc(state.native_handle(), LUA_GCCOLLECT, 0);

    EXPECT_EQ(std::get<std::int64_t>(table.get("field")), 91);
    EXPECT_EQ(state.top(), 0);
}

TEST(TableTests, HandleRemainsValidWhenStateIsMoved) {
    lunaris::State original;
    lunaris::Table table = original.create_table();
    table.set("field", lunaris::Value{std::int64_t{5}});

    lunaris::State moved(std::move(original));
    EXPECT_EQ(std::get<std::int64_t>(table.get("field")), 5);

    lunaris::State destination;
    destination = std::move(moved);
    table.set("field", lunaris::Value{std::int64_t{9}});
    EXPECT_EQ(std::get<std::int64_t>(table.get("field")), 9);
}

TEST(TableTests, HandleBecomesInvalidWhenStateIsDestroyed) {
    std::optional<lunaris::Table> table;
    {
        lunaris::State state;
        table.emplace(state.create_table());
    }

    EXPECT_THROW((void)table->get("field"), std::logic_error);
    table.reset();
}

TEST(TableTests, GetTableValidatesStackValue) {
    lunaris::State state;
    state.push(1);

    EXPECT_THROW((void)state.get_table(-1), std::invalid_argument);
    EXPECT_THROW((void)state.get_table(2), std::out_of_range);
}
