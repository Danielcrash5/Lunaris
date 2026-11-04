#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

struct Counter {
    explicit Counter(int initialValue) : value(initialValue) {
    }

    void add(int amount) {
        value += amount;
    }

    int current() const {
        return value;
    }

    int value;
    const int identifier = 7;
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
        "Counter", lunaris::constructor<Counter, int>(),
        lunaris::property("value", &Counter::value),
        lunaris::property("identifier", &Counter::identifier),
        lunaris::method("add", &Counter::add),
        lunaris::method("current", &Counter::current));

    auto script = state.load_script("local counter = Counter.new(4); "
                                    "counter:add(3); counter.value = 9; "
                                    "return counter:current(), counter.value",
                                    "binding-test.luau");
    ASSERT_TRUE(script);
    auto result = script.value().execute();

    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result.value().size(), 2U);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[0]), 9);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[1]), 9);
}

TEST(BindingTests, ReportsInvalidPropertyTypesAndUsertypeMismatches) {
    lunaris::State state;
    state.new_usertype<Counter>(
        "Counter", lunaris::constructor<Counter, int>(),
        lunaris::property("value", &Counter::value),
        lunaris::property("identifier", &Counter::identifier),
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

    auto badArity = state.load_script("local counter = Counter.new(1, 2)",
                                      "bad-constructor-arity.luau");
    ASSERT_TRUE(badArity);
    auto badArityResult = badArity.value().execute();
    ASSERT_FALSE(badArityResult);
    EXPECT_NE(badArityResult.error().message().find("wrong number"),
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
