#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

struct Counter {
    Counter() = default;
    explicit Counter(int initialValue) : value(initialValue) {
    }
    explicit Counter(std::string_view initialName) : name(initialName) {
    }

    void add(int amount) {
        value += amount;
    }

    int current() const {
        return value;
    }

    int get_health() const {
        return health;
    }

    void set_health(int newHealth) {
        health = newHealth;
    }

    std::string identify(int) const {
        return "integer";
    }

    std::string identify(std::string_view) const {
        return "string";
    }

    int choose(int) const {
        return 1;
    }

    int choose(double) const {
        return 2;
    }

    int value;
    int health = 100;
    const int identifier = 7;
    std::string name;
};

struct LifetimeProbe {
    LifetimeProbe() {
        ++constructed;
    }
    ~LifetimeProbe() {
        ++destroyed;
    }

    static int constructed;
    static int destroyed;
};

int LifetimeProbe::constructed = 0;
int LifetimeProbe::destroyed = 0;

} // namespace

TEST(BindingTests, RegistersOwnedUsertypesWithPropertiesAndMethods) {
    lunaris::State state;
    state.new_usertype<Counter>(
        "Counter",
        lunaris::constructors(
            lunaris::constructor<Counter>(),
            lunaris::constructor<Counter, int>(),
            lunaris::constructor<Counter, std::string_view>()),
        lunaris::property("value", &Counter::value),
        lunaris::property("identifier", &Counter::identifier),
        lunaris::property("health", &Counter::get_health, &Counter::set_health),
        lunaris::property("currentValue", &Counter::current),
        lunaris::method("add", &Counter::add),
        lunaris::method("current", &Counter::current),
        lunaris::method(
            "identify",
            lunaris::overload(
                static_cast<std::string (Counter::*)(int) const>(
                    &Counter::identify),
                static_cast<std::string (Counter::*)(std::string_view) const>(
                    &Counter::identify))),
        lunaris::method(
            "choose",
            lunaris::overload(
                static_cast<int (Counter::*)(int) const>(&Counter::choose),
                static_cast<int (Counter::*)(double) const>(
                    &Counter::choose))));

    auto script = state.load_script("local counter = Counter.new(4); "
                                    "counter:add(3); counter.value = 9; "
                                    "counter.health = 75; "
                                    "return counter:current(), "
                                    "counter.currentValue, counter.health, "
                                    "counter:identify(2), "
                                    "counter:identify('x')",
                                    "binding-test.luau");
    ASSERT_TRUE(script);
    auto result = script.value().execute();

    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result.value().size(), 5U);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[0]), 9);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[1]), 9);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[2]), 75);
    EXPECT_EQ(std::get<std::string>(result.value()[3]), "integer");
    EXPECT_EQ(std::get<std::string>(result.value()[4]), "string");
}

TEST(BindingTests, ReportsInvalidPropertyTypesAndUsertypeMismatches) {
    lunaris::State state;
    state.new_usertype<Counter>(
        "Counter",
        lunaris::constructors(
            lunaris::constructor<Counter>(),
            lunaris::constructor<Counter, int>(),
            lunaris::constructor<Counter, std::string_view>()),
        lunaris::property("value", &Counter::value),
        lunaris::property("identifier", &Counter::identifier),
        lunaris::property("health", &Counter::get_health, &Counter::set_health),
        lunaris::property("currentValue", &Counter::current),
        lunaris::method("current", &Counter::current));
    state.new_usertype<LifetimeProbe>("LifetimeProbe",
                                      lunaris::constructor<LifetimeProbe>());

    auto badProperty = state.load_script(
        "local counter = Counter.new(1); counter.value = 'wrong'",
        "bad-property.luau");
    ASSERT_TRUE(badProperty);
    auto badPropertyResult = badProperty.value().execute();
    ASSERT_FALSE(badPropertyResult);
    EXPECT_NE(badPropertyResult.error().message().find("expected an in-range"),
              std::string_view::npos);

    auto readOnly = state.load_script(
        "local counter = Counter.new(1); counter.identifier = 2",
        "read-only-property.luau");
    ASSERT_TRUE(readOnly);
    auto readOnlyResult = readOnly.value().execute();
    ASSERT_FALSE(readOnlyResult);
    EXPECT_NE(readOnlyResult.error().message().find("read-only"),
              std::string_view::npos)
        << readOnlyResult.error().message();

    auto readOnlyGetter = state.load_script(
        "local counter = Counter.new(1); counter.currentValue = 2",
        "read-only-getter.luau");
    ASSERT_TRUE(readOnlyGetter);
    auto readOnlyGetterResult = readOnlyGetter.value().execute();
    ASSERT_FALSE(readOnlyGetterResult);
    EXPECT_NE(readOnlyGetterResult.error().message().find("read-only"),
              std::string_view::npos)
        << readOnlyGetterResult.error().message();

    auto invalidSetter = state.load_script(
        "local counter = Counter.new(1); counter.health = 'wrong'",
        "invalid-setter.luau");
    ASSERT_TRUE(invalidSetter);
    auto invalidSetterResult = invalidSetter.value().execute();
    ASSERT_FALSE(invalidSetterResult);
    EXPECT_NE(
        invalidSetterResult.error().message().find("expected an in-range"),
        std::string_view::npos);

    auto badArity = state.load_script("local counter = Counter.new(true)",
                                      "bad-constructor-arity.luau");
    ASSERT_TRUE(badArity);
    auto badArityResult = badArity.value().execute();
    ASSERT_FALSE(badArityResult);
    EXPECT_NE(badArityResult.error().message().find("no constructor overload"),
              std::string_view::npos);

    auto wrongReceiver = state.load_script(
        "local counter = Counter.new(1); "
        "local probe = LifetimeProbe.new(); "
        "local current = counter.current; return current(probe)",
        "wrong-receiver.luau");
    ASSERT_TRUE(wrongReceiver);
    auto wrongReceiverResult = wrongReceiver.value().execute();
    ASSERT_FALSE(wrongReceiverResult);
    EXPECT_NE(wrongReceiverResult.error().message().find("expected userdata"),
              std::string_view::npos)
        << wrongReceiverResult.error().message();
}

TEST(BindingTests, DestroysOwnedUserdataWhenStateCloses) {
    LifetimeProbe::constructed = 0;
    LifetimeProbe::destroyed = 0;
    {
        lunaris::State state;
        state.new_usertype<LifetimeProbe>(
            "LifetimeProbe", lunaris::constructor<LifetimeProbe>());
        auto script = state.load_script("local object = LifetimeProbe.new()",
                                        "lifetime.luau");
        ASSERT_TRUE(script);
        auto result = script.value().execute();
        ASSERT_TRUE(result) << result.error().message();
        EXPECT_EQ(LifetimeProbe::constructed, 1);
        EXPECT_EQ(LifetimeProbe::destroyed, 0);
    }
    EXPECT_EQ(LifetimeProbe::destroyed, 1);
}

TEST(BindingTests, SelectsConstructorOverloadsAndReportsAmbiguousMethods) {
    lunaris::State state;
    state.new_usertype<Counter>(
        "Counter",
        lunaris::constructors(
            lunaris::constructor<Counter>(),
            lunaris::constructor<Counter, int>(),
            lunaris::constructor<Counter, std::string_view>()),
        lunaris::property("value", &Counter::value),
        lunaris::property("name", &Counter::name),
        lunaris::method(
            "choose",
            lunaris::overload(
                static_cast<int (Counter::*)(int) const>(&Counter::choose),
                static_cast<int (Counter::*)(double) const>(
                    &Counter::choose))));

    auto constructors =
        state.load_script("local default = Counter.new(); "
                          "local integer = Counter.new(8); "
                          "local named = Counter.new('named'); "
                          "return default.value, integer.value, named.name",
                          "constructor-overloads.luau");
    ASSERT_TRUE(constructors);
    auto constructorResult = constructors.value().execute();
    ASSERT_TRUE(constructorResult) << constructorResult.error().message();
    ASSERT_EQ(constructorResult.value().size(), 3U);
    EXPECT_EQ(std::get<std::int64_t>(constructorResult.value()[0]), 0);
    EXPECT_EQ(std::get<std::int64_t>(constructorResult.value()[1]), 8);
    EXPECT_EQ(std::get<std::string>(constructorResult.value()[2]), "named");

    auto ambiguous = state.load_script(
        "local counter = Counter.new(); return counter:choose(1)",
        "ambiguous-overload.luau");
    ASSERT_TRUE(ambiguous);
    auto ambiguousResult = ambiguous.value().execute();
    ASSERT_FALSE(ambiguousResult);
    EXPECT_NE(ambiguousResult.error().message().find("is ambiguous"),
              std::string_view::npos)
        << ambiguousResult.error().message();

    auto unmatched = state.load_script(
        "local counter = Counter.new(); return counter:choose(true)",
        "unmatched-overload.luau");
    ASSERT_TRUE(unmatched);
    auto unmatchedResult = unmatched.value().execute();
    ASSERT_FALSE(unmatchedResult);
    EXPECT_NE(unmatchedResult.error().message().find("no overload"),
              std::string_view::npos)
        << unmatchedResult.error().message();
}

TEST(BindingTests, RejectsRepeatedTypeAndMemberRegistration) {
    lunaris::State state;
    state.new_usertype<Counter>("Counter", lunaris::constructor<Counter, int>(),
                                lunaris::property("value", &Counter::value));
    EXPECT_THROW(state.new_usertype<Counter>(
                     "Counter", lunaris::constructor<Counter, int>(),
                     lunaris::property("value", &Counter::value)),
                 std::invalid_argument);

    EXPECT_THROW(state.new_usertype<Counter>(
                     "OtherCounter", lunaris::constructor<Counter, int>(),
                     lunaris::property("value", &Counter::value),
                     lunaris::property("value", &Counter::value)),
                 std::invalid_argument);
}
