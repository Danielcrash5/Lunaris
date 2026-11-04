#include "lunaris/Actor.h"
#include "lunaris/Scheduler.h"
#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class ManualScheduler final : public lunaris::Scheduler {
  public:
    lunaris::Result<void> schedule(Task task) override {
        if (m_reject) {
            return lunaris::Result<void>::failure(lunaris::Error(
                lunaris::ErrorType::Runtime, "scheduler is stopping"));
        }
        m_tasks.push_back(std::move(task));
        return lunaris::Result<void>::success();
    }

    void run_next() {
        Task task = std::move(m_tasks.front());
        m_tasks.erase(m_tasks.begin());
        task();
    }

    void reject() {
        m_reject = true;
    }

    [[nodiscard]] std::size_t pending() const {
        return m_tasks.size();
    }

  private:
    std::vector<Task> m_tasks;
    bool m_reject = false;
};

} // namespace

TEST(ActorTests, DispatchesMessagesInFifoOrder) {
    lunaris::Actor actor;
    std::vector<std::int64_t> received;
    actor.bind("Work", [&](const lunaris::Message &message) {
        received.push_back(std::get<std::int64_t>(message.payload));
        return lunaris::Result<void>::success();
    });

    ASSERT_TRUE(actor.send("Work", lunaris::Value{std::int64_t{1}}));
    ASSERT_TRUE(actor.send("Work", lunaris::Value{std::int64_t{2}}));
    EXPECT_EQ(actor.pending(), 2U);

    auto first = actor.dispatch_one();
    ASSERT_TRUE(first);
    EXPECT_TRUE(first.value());
    EXPECT_EQ(received, (std::vector<std::int64_t>{1}));

    auto rest = actor.dispatch_pending();
    ASSERT_TRUE(rest);
    EXPECT_EQ(rest.value(), 1U);
    EXPECT_EQ(received, (std::vector<std::int64_t>{1, 2}));
    EXPECT_EQ(actor.pending(), 0U);
}

TEST(ActorTests, ReportsMissingHandlerAndConsumesThatMessage) {
    lunaris::Actor actor;
    ASSERT_TRUE(actor.send("Missing", lunaris::Value{}));

    auto result = actor.dispatch_one();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Runtime);
    EXPECT_NE(result.error().message().find("Missing"), std::string_view::npos);
    EXPECT_EQ(actor.pending(), 0U);
}

TEST(ActorTests, RejectsInvalidAndDuplicateBindings) {
    lunaris::Actor actor;
    auto handler = [](const lunaris::Message &) {
        return lunaris::Result<void>::success();
    };

    EXPECT_THROW(actor.bind("", handler), std::invalid_argument);
    EXPECT_THROW(actor.bind(std::string_view{"Bad\0Topic", 9}, handler),
                 std::invalid_argument);
    EXPECT_THROW(actor.bind("Empty", {}), std::invalid_argument);
    actor.bind("Update", handler);
    EXPECT_THROW(actor.bind("Update", handler), std::invalid_argument);
    EXPECT_TRUE(actor.unbind("Update"));
    EXPECT_FALSE(actor.unbind("Update"));
}

TEST(ActorTests, PropagatesHandlerErrorsAndExceptions) {
    lunaris::Actor actor;
    actor.bind("Error", [](const lunaris::Message &) {
        return lunaris::Result<void>::failure(
            lunaris::Error(lunaris::ErrorType::Runtime, "handler failed"));
    });
    actor.bind("Throw", [](const lunaris::Message &) -> lunaris::Result<void> {
        throw std::runtime_error("handler threw");
    });

    ASSERT_TRUE(actor.send("Error", lunaris::Value{}));
    auto error = actor.dispatch_one();
    ASSERT_FALSE(error);
    EXPECT_EQ(error.error().message(), "handler failed");

    ASSERT_TRUE(actor.send("Throw", lunaris::Value{}));
    auto thrown = actor.dispatch_one();
    ASSERT_FALSE(thrown);
    EXPECT_EQ(thrown.error().type(), lunaris::ErrorType::Runtime);
    EXPECT_NE(thrown.error().message().find("handler threw"),
              std::string_view::npos);
}

TEST(ActorTests, RejectsReentrantDispatchWithoutDeadlocking) {
    lunaris::Actor actor;
    bool reentrantDispatchFailed = false;
    actor.bind("Outer", [&](const lunaris::Message &) {
        auto nested = actor.dispatch_one();
        reentrantDispatchFailed = !nested;
        return lunaris::Result<void>::success();
    });
    actor.bind("Inner", [](const lunaris::Message &) {
        return lunaris::Result<void>::success();
    });
    ASSERT_TRUE(actor.send("Outer", lunaris::Value{}));
    ASSERT_TRUE(actor.send("Inner", lunaris::Value{}));

    auto result = actor.dispatch_pending();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 2U);
    EXPECT_TRUE(reentrantDispatchFailed);
}

TEST(ActorTests, CloseRejectsNewMessagesAndDrainsExistingOnDispatch) {
    lunaris::Actor actor;
    std::size_t handled = 0;
    actor.bind("Work", [&](const lunaris::Message &) {
        ++handled;
        return lunaris::Result<void>::success();
    });
    ASSERT_TRUE(actor.send("Work", lunaris::Value{}));
    actor.close();

    EXPECT_TRUE(actor.is_closed());
    EXPECT_FALSE(actor.send("Work", lunaris::Value{}));
    auto dispatched = actor.dispatch_pending();
    ASSERT_TRUE(dispatched);
    EXPECT_EQ(dispatched.value(), 1U);
    EXPECT_EQ(handled, 1U);
}

TEST(ActorTests, HostSchedulerControlsActorDispatch) {
    lunaris::Actor actor;
    ManualScheduler scheduler;
    std::size_t handled = 0;
    actor.bind("Work", [&](const lunaris::Message &) {
        ++handled;
        return lunaris::Result<void>::success();
    });
    ASSERT_TRUE(actor.send("Work", lunaris::Value{}));

    auto scheduled = scheduler.schedule([&actor] {
        auto result = actor.dispatch_pending();
        if (!result) {
            throw std::runtime_error(std::string(result.error().message()));
        }
    });
    ASSERT_TRUE(scheduled);
    EXPECT_EQ(handled, 0U);
    ASSERT_EQ(scheduler.pending(), 1U);

    scheduler.run_next();
    EXPECT_EQ(handled, 1U);

    scheduler.reject();
    auto rejected = scheduler.schedule([] {});
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().message(), "scheduler is stopping");
}

TEST(ActorTests, LuauSendsScalarMessageToHostOwnedActor) {
    auto actor = std::make_shared<lunaris::Actor>();
    lunaris::State sender;
    lunaris::Actor::install(sender, "target", actor);

    std::string receivedTopic;
    lunaris::Value receivedPayload;
    actor->bind("Update", [&](const lunaris::Message &message) {
        receivedTopic = message.topic;
        receivedPayload = message.payload;
        return lunaris::Result<void>::success();
    });

    auto script = sender.load_script("return target:SendMessage('Update', 42)",
                                     "actor-send-message.luau");
    ASSERT_TRUE(script);
    auto sent = script.value().execute();
    ASSERT_TRUE(sent) << sent.error().message();
    ASSERT_EQ(sent.value().size(), 1U);
    EXPECT_TRUE(std::get<bool>(sent.value()[0]));
    EXPECT_EQ(actor->pending(), 1U);
    EXPECT_TRUE(receivedTopic.empty());

    auto dispatched = actor->dispatch_pending();
    ASSERT_TRUE(dispatched);
    EXPECT_EQ(dispatched.value(), 1U);
    EXPECT_EQ(receivedTopic, "Update");
    EXPECT_EQ(std::get<double>(receivedPayload), 42.0);
}

TEST(ActorTests, LuauActorHandleRejectsUnsupportedPayloadsAndClosedActors) {
    auto actor = std::make_shared<lunaris::Actor>();
    lunaris::State state;
    lunaris::Actor::install(state, "target", actor);

    auto invalid = state.load_script("target:SendMessage('Update', {})",
                                     "actor-invalid-payload.luau");
    ASSERT_TRUE(invalid);
    auto invalidResult = invalid.value().execute();
    ASSERT_FALSE(invalidResult);
    EXPECT_EQ(invalidResult.error().type(), lunaris::ErrorType::Runtime);

    actor->close();
    auto closed = state.load_script("return target:SendMessage('Update', nil)",
                                    "actor-send-closed.luau");
    ASSERT_TRUE(closed);
    auto closedResult = closed.value().execute();
    ASSERT_TRUE(closedResult) << closedResult.error().message();
    ASSERT_EQ(closedResult.value().size(), 1U);
    EXPECT_FALSE(std::get<bool>(closedResult.value()[0]));
}

TEST(ActorTests, LuauActorInstallationValidatesNameAndGlobal) {
    lunaris::State state;
    auto actor = std::make_shared<lunaris::Actor>();

    EXPECT_THROW(lunaris::Actor::install(state, "", actor),
                 std::invalid_argument);
    EXPECT_THROW(
        lunaris::Actor::install(state, std::string_view{"bad\0name", 8}, actor),
        std::invalid_argument);
    ASSERT_TRUE(state.set_global("existing", lunaris::Value{true}));
    EXPECT_THROW(lunaris::Actor::install(state, "existing", actor),
                 std::invalid_argument);
    EXPECT_THROW(lunaris::Actor::install(state, "Actor", nullptr),
                 std::invalid_argument);
    EXPECT_NO_THROW(lunaris::Actor::install(state, "target", actor));
    EXPECT_NO_THROW(lunaris::Actor::install(
        state, "other", std::make_shared<lunaris::Actor>()));
    EXPECT_THROW(lunaris::Actor::install(state, "target", actor),
                 std::invalid_argument);
}
