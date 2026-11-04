#include "lunaris/State.h"

#include <gtest/gtest.h>
#include <lua.h>

#include <cstdint>
#include <memory>
#include <string>

namespace {

enum class PlayerMode : std::int32_t { Idle = 0, Running = 1 };
enum class RenderFlag : std::uint32_t { Visible = 1, CastShadow = 2 };

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

    void set_mode(PlayerMode newMode) {
        mode = newMode;
    }

    PlayerMode get_mode() const {
        return mode;
    }

    void set_render_flag(RenderFlag newFlag) {
        renderFlag = newFlag;
    }

    RenderFlag get_render_flag() const {
        return renderFlag;
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
    PlayerMode mode = PlayerMode::Idle;
    RenderFlag renderFlag = RenderFlag::Visible;
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

    int value() const {
        return 42;
    }

    static int constructed;
    static int destroyed;
};

int LifetimeProbe::constructed = 0;
int LifetimeProbe::destroyed = 0;

int add_values(int left, int right) {
    return left + right;
}

} // namespace

TEST(BindingTests, RegistersOwnedUsertypesWithPropertiesAndMethods) {
    lunaris::State state;
    state.new_enum<PlayerMode>(
        "PlayerMode",
        {{"Idle", PlayerMode::Idle}, {"Running", PlayerMode::Running}});
    state.new_flags<RenderFlag>("RenderFlags",
                                {{"Visible", RenderFlag::Visible},
                                 {"CastShadow", RenderFlag::CastShadow}});
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
        lunaris::method("setMode", &Counter::set_mode),
        lunaris::method("getMode", &Counter::get_mode),
        lunaris::method("setRenderFlag", &Counter::set_render_flag),
        lunaris::method("getRenderFlag", &Counter::get_render_flag),
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

    auto script =
        state.load_script("local counter = Counter.new(4); "
                          "counter:add(3); counter.value = 9; "
                          "counter.health = 75; "
                          "counter:setMode(PlayerMode.Running); "
                          "counter:setRenderFlag(RenderFlags.CastShadow); "
                          "return counter:current(), "
                          "counter.currentValue, counter.health, "
                          "counter:identify(2), "
                          "counter:identify('x'), "
                          "counter:getMode(), "
                          "counter:getRenderFlag(), "
                          "PlayerMode.Idle, RenderFlags.Visible",
                          "binding-test.luau");
    ASSERT_TRUE(script) << script.error().message();
    auto result = script.value().execute();

    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result.value().size(), 9U);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[0]), 9);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[1]), 9);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[2]), 75);
    EXPECT_EQ(std::get<std::string>(result.value()[3]), "integer");
    EXPECT_EQ(std::get<std::string>(result.value()[4]), "string");
    EXPECT_EQ(std::get<std::int64_t>(result.value()[5]),
              static_cast<std::int64_t>(PlayerMode::Running));
    EXPECT_EQ(std::get<std::int64_t>(result.value()[6]),
              static_cast<std::int64_t>(RenderFlag::CastShadow));
    EXPECT_EQ(std::get<std::int64_t>(result.value()[7]),
              static_cast<std::int64_t>(PlayerMode::Idle));
    EXPECT_EQ(std::get<std::int64_t>(result.value()[8]),
              static_cast<std::int64_t>(RenderFlag::Visible));
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

TEST(BindingTests, RejectsInvalidAndRepeatedEnumRegistrations) {
    lunaris::State state;
    state.new_enum<PlayerMode>(
        "PlayerMode",
        {{"Idle", PlayerMode::Idle}, {"Running", PlayerMode::Running}});
    EXPECT_THROW(
        state.new_enum<PlayerMode>("PlayerMode", {{"Idle", PlayerMode::Idle}}),
        std::invalid_argument);
    EXPECT_THROW(state.new_flags<RenderFlag>(
                     "Invalid", {{"Visible", RenderFlag::Visible},
                                 {"Visible", RenderFlag::CastShadow}}),
                 std::invalid_argument);
    EXPECT_THROW(
        state.new_enum<PlayerMode>("BadName", {{"", PlayerMode::Idle}}),
        std::invalid_argument);
}

TEST(BindingTests, EnumAndFlagTablesAreReadOnly) {
    lunaris::State state;
    state.new_enum<PlayerMode>(
        "PlayerMode",
        {{"Idle", PlayerMode::Idle}, {"Running", PlayerMode::Running}});
    state.new_flags<RenderFlag>("RenderFlags",
                                {{"Visible", RenderFlag::Visible},
                                 {"CastShadow", RenderFlag::CastShadow}});

    auto script = state.load_script(
        "PlayerMode.Idle = 9; RenderFlags.Visible = 0", "readonly-enums.luau");
    ASSERT_TRUE(script);
    auto result = script.value().execute();
    ASSERT_FALSE(result);
}

TEST(BindingTests, BorrowedUserdataDoesNotDestroyTheHostObject) {
    LifetimeProbe::constructed = 0;
    LifetimeProbe::destroyed = 0;
    LifetimeProbe object;
    {
        lunaris::State state;
        state.new_usertype<LifetimeProbe>(
            "LifetimeProbe", lunaris::method("value", &LifetimeProbe::value));
        int wrongType = 0;
        EXPECT_THROW(state.push_borrowed("LifetimeProbe", wrongType),
                     std::invalid_argument);
        EXPECT_EQ(state.top(), 0);
        state.push_borrowed("LifetimeProbe", object);
        lua_setglobal(state.native_handle(), "hostObject");

        auto script = state.load_script("return hostObject:value()",
                                        "borrowed-userdata.luau");
        ASSERT_TRUE(script);
        auto result = script.value().execute();
        ASSERT_TRUE(result) << result.error().message();
        ASSERT_EQ(result.value().size(), 1);
        EXPECT_EQ(std::get<std::int64_t>(result.value()[0]), 42);
        lua_gc(state.native_handle(), LUA_GCCOLLECT, 0);
        EXPECT_EQ(LifetimeProbe::destroyed, 0);
    }
    EXPECT_EQ(LifetimeProbe::destroyed, 0);
}

TEST(BindingTests, SharedUserdataLivesUntilLuauReleasesIt) {
    LifetimeProbe::constructed = 0;
    LifetimeProbe::destroyed = 0;
    lunaris::State state;
    state.new_usertype<LifetimeProbe>(
        "LifetimeProbe", lunaris::method("value", &LifetimeProbe::value));
    auto object = std::make_shared<LifetimeProbe>();
    state.push_shared("LifetimeProbe", object);
    lua_setglobal(state.native_handle(), "hostObject");
    object.reset();
    EXPECT_EQ(LifetimeProbe::destroyed, 0);

    auto script = state.load_script(
        "local value = hostObject:value(); hostObject = nil; return value",
        "shared-userdata.luau");
    ASSERT_TRUE(script);
    auto result = script.value().execute();
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(LifetimeProbe::destroyed, 0);
    lua_pushnil(state.native_handle());
    lua_setglobal(state.native_handle(), "hostObject");
    lua_gc(state.native_handle(), LUA_GCCOLLECT, 0);
    EXPECT_EQ(LifetimeProbe::destroyed, 1);
}

TEST(BindingTests, RegistersFunctionsConstantsEnumsAndUsertypesInLibrary) {
    Counter hostObject(11);
    lunaris::State state;
    auto library = state.create_library("Math");
    library.set_constant("offset", std::int64_t{3});
    library.set_function("add", &add_values);
    library.new_enum<PlayerMode>(
        "Mode", {{"Idle", PlayerMode::Idle}, {"Running", PlayerMode::Running}});
    library.new_usertype<Counter>(
        "Counter", lunaris::constructor<Counter, int>(),
        lunaris::property("value", &Counter::value),
        lunaris::method("current", &Counter::current));
    library.push_borrowed("Counter", hostObject);
    lua_setglobal(state.native_handle(), "hostCounter");

    auto script = state.load_script(
        "return Math.add(Math.offset, 4), Math.Mode.Running, "
        "Math.Counter.new(9):current(), hostCounter:current()",
        "library-bindings.luau");
    ASSERT_TRUE(script);
    auto result = script.value().execute();
    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result.value().size(), 4);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[0]), 7);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[1]),
              static_cast<std::int64_t>(PlayerMode::Running));
    EXPECT_EQ(std::get<std::int64_t>(result.value()[2]), 9);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[3]), 11);
    EXPECT_THROW(library.set_constant("offset", std::int64_t{5}),
                 std::invalid_argument);

    auto readonly =
        state.load_script("Math.offset = 10", "readonly-library.luau");
    ASSERT_TRUE(readonly);
    EXPECT_FALSE(readonly.value().execute());
}

TEST(BindingTests, RequiresRegisteredLibraryWithoutReplacingItsGlobal) {
    lunaris::State state;
    auto library = state.create_library("Math");
    library.set_constant("offset", std::int64_t{3});
    library.set_function("add", &add_values);
    library.register_module("math");
    lua_getglobal(state.native_handle(), "require");
    EXPECT_EQ(lua_type(state.native_handle(), -1), LUA_TFUNCTION);
    lua_pop(state.native_handle(), 1);

    auto script = state.load_script(
        "local required = require('math'); "
        "return required.add(required.offset, 4), required == Math, "
        "required == require('math')",
        "native-library-require.luau");
    ASSERT_TRUE(script);
    auto result = script.value().execute();
    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result.value().size(), 3U);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[0]), 7);
    EXPECT_TRUE(std::get<bool>(result.value()[1]));
    EXPECT_TRUE(std::get<bool>(result.value()[2]));

    state.set_module_loader(nullptr);
    auto after_loader_reset =
        state.load_script("return require('math').offset",
                          "native-library-require-after-reset.luau");
    ASSERT_TRUE(after_loader_reset);
    auto after_reset_result = after_loader_reset.value().execute();
    ASSERT_TRUE(after_reset_result) << after_reset_result.error().message();
    ASSERT_EQ(after_reset_result.value().size(), 1U);
    EXPECT_EQ(std::get<std::int64_t>(after_reset_result.value()[0]), 3);

    EXPECT_THROW(library.register_module("math"), std::invalid_argument);
    EXPECT_NO_THROW(library.register_module("game/math"));
}

TEST(BindingTests, CanExposeLibraryOnlyThroughRequire) {
    lunaris::State state;
    auto library = state.create_library("InternalMath");
    library.set_constant("offset", std::int64_t{8});
    library.register_module("private.math",
                            lunaris::LibraryVisibility::ModuleOnly);

    lua_getglobal(state.native_handle(), "InternalMath");
    EXPECT_EQ(lua_type(state.native_handle(), -1), LUA_TNIL);
    lua_pop(state.native_handle(), 1);

    auto script = state.load_script(
        "return InternalMath == nil, require('private.math').offset",
        "module-only-library.luau");
    ASSERT_TRUE(script);
    auto result = script.value().execute();
    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result.value().size(), 2U);
    EXPECT_TRUE(std::get<bool>(result.value()[0]));
    EXPECT_EQ(std::get<std::int64_t>(result.value()[1]), 8);
}
