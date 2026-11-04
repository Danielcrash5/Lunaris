#include "lunaris/Future.h"
#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <barrier>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

TEST(FutureTests, ReportsPendingThenReturnsValueOnce) {
    lunaris::Promise<int> promise;
    auto future = promise.get_future();

    EXPECT_EQ(future.status(), lunaris::FutureStatus::Pending);
    EXPECT_FALSE(future.wait_for(std::chrono::milliseconds(1)));
    ASSERT_TRUE(promise.set_value(42));
    EXPECT_EQ(future.status(), lunaris::FutureStatus::Succeeded);
    ASSERT_TRUE(future.wait_for(std::chrono::milliseconds(0)));

    auto result = future.get();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 42);
    EXPECT_THROW((void)future.get(), std::logic_error);
}

TEST(FutureTests, SupportsVoidAndMoveOnlyResults) {
    lunaris::Promise<void> voidPromise;
    auto voidFuture = voidPromise.get_future();
    ASSERT_TRUE(voidPromise.set_value());
    EXPECT_TRUE(voidFuture.get());

    lunaris::Promise<std::unique_ptr<int>> movePromise;
    auto moveFuture = movePromise.get_future();
    ASSERT_TRUE(movePromise.set_value(std::make_unique<int>(23)));
    auto moveResult = moveFuture.get();
    ASSERT_TRUE(moveResult);
    ASSERT_NE(moveResult.value(), nullptr);
    EXPECT_EQ(*moveResult.value(), 23);
}

TEST(FutureTests, PropagatesErrors) {
    lunaris::Promise<int> promise;
    auto future = promise.get_future();

    ASSERT_TRUE(promise.set_error(
        lunaris::Error(lunaris::ErrorType::Runtime, "task failed")));
    EXPECT_EQ(future.status(), lunaris::FutureStatus::Failed);

    auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Runtime);
    EXPECT_EQ(result.error().message(), "task failed");
    EXPECT_FALSE(promise.set_value(1));
}

TEST(FutureTests, CancellationWinsOnceWithoutInterruptingProducer) {
    lunaris::Promise<int> promise;
    auto future = promise.get_future();

    ASSERT_TRUE(future.cancel());
    EXPECT_EQ(future.status(), lunaris::FutureStatus::Cancelled);
    EXPECT_FALSE(future.cancel());
    EXPECT_FALSE(promise.set_value(1));

    auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Cancelled);
}

TEST(FutureTests, AbandonedPromiseFailsTheFuture) {
    lunaris::Future<int> future;
    {
        lunaris::Promise<int> promise;
        future = promise.get_future();
    }

    EXPECT_EQ(future.status(), lunaris::FutureStatus::Failed);
    auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Future);
}

TEST(FutureTests, AllowsOnlyOneFuturePerPromise) {
    lunaris::Promise<void> promise;
    auto future = promise.get_future();

    EXPECT_THROW((void)promise.get_future(), std::logic_error);
    ASSERT_TRUE(promise.set_value());
    EXPECT_TRUE(future.get());
}

TEST(FutureTests, CompletionAndCancellationRaceHasOneWinner) {
    lunaris::Promise<int> promise;
    auto future = promise.get_future();
    std::barrier start(3);
    bool completed = false;
    bool cancelled = false;

    std::thread producer([&] {
        start.arrive_and_wait();
        completed = promise.set_value(23);
    });
    std::thread canceller([&] {
        start.arrive_and_wait();
        cancelled = future.cancel();
    });
    start.arrive_and_wait();
    producer.join();
    canceller.join();

    EXPECT_NE(completed, cancelled);
    auto result = future.get();
    if (completed) {
        ASSERT_TRUE(result);
        EXPECT_EQ(result.value(), 23);
    } else {
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().type(), lunaris::ErrorType::Cancelled);
    }
}

TEST(FutureTests, ReadinessSignalTracksCompletionAndCancellation) {
    lunaris::Promise<int> promise;
    auto future = promise.get_future();
    auto signal = std::make_shared<std::atomic_bool>(false);
    future.observe_ready(signal);
    EXPECT_FALSE(signal->load(std::memory_order_acquire));

    ASSERT_TRUE(promise.set_value(1));
    EXPECT_TRUE(signal->load(std::memory_order_acquire));

    lunaris::Promise<int> cancelledPromise;
    auto cancelledFuture = cancelledPromise.get_future();
    auto cancelledSignal = std::make_shared<std::atomic_bool>(false);
    cancelledFuture.observe_ready(cancelledSignal);
    ASSERT_TRUE(cancelledFuture.cancel());
    EXPECT_TRUE(cancelledSignal->load(std::memory_order_acquire));
}

TEST(FutureTests, LuauAwaitYieldsAndResumesOnlyWhenHostPumpsState) {
    lunaris::State state;
    state.open_coroutine_library();
    lunaris::Promise<lunaris::Value> promise;
    state.set_future_global("pending", promise.get_future());

    auto script = state.load_script(
        "local coroutineHandle = coroutine.create(function() "
        "resolved = pending:await() end); "
        "local ok, err = coroutine.resume(coroutineHandle); return ok, err",
        "future-await.luau");
    ASSERT_TRUE(script);
    auto initial = script.value().execute();
    ASSERT_TRUE(initial) << initial.error().message();
    ASSERT_EQ(initial.value().size(), 2U);
    EXPECT_TRUE(std::get<bool>(initial.value()[0]));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(initial.value()[1]));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(
        script.value().get_global("resolved").value()));
    EXPECT_EQ(state.resume_ready().value(), 0U);

    ASSERT_TRUE(promise.set_value(lunaris::Value{std::int64_t{42}}));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(
        script.value().get_global("resolved").value()));
    auto resumed = state.resume_ready();
    ASSERT_TRUE(resumed);
    EXPECT_EQ(resumed.value(), 1U);
    auto resolved = script.value().get_global("resolved");
    ASSERT_TRUE(resolved);
    EXPECT_EQ(std::get<std::int64_t>(resolved.value()), 42);
}

TEST(FutureTests, LuauAwaitReturnsReadyValueImmediately) {
    lunaris::State state;
    state.open_coroutine_library();
    lunaris::Promise<lunaris::Value> promise;
    ASSERT_TRUE(promise.set_value(lunaris::Value{std::string{"ready"}}));
    state.set_future_global("readyFuture", promise.get_future());

    auto script = state.load_script(
        "local co = coroutine.create(function() "
        "resolved = readyFuture:await() end); return coroutine.resume(co)",
        "future-await-ready.luau");
    ASSERT_TRUE(script);
    auto executed = script.value().execute();
    ASSERT_TRUE(executed) << executed.error().message();
    auto resolved = script.value().get_global("resolved");
    ASSERT_TRUE(resolved);
    EXPECT_EQ(std::get<std::string>(resolved.value()), "ready");
}

TEST(FutureTests, LuauAwaitPropagatesFailuresDuringResume) {
    lunaris::State state;
    state.open_coroutine_library();
    lunaris::Promise<lunaris::Value> promise;
    state.set_future_global("failedFuture", promise.get_future());
    auto script = state.load_script(
        "local co = coroutine.create(function() "
        "failedFuture:await() end); return coroutine.resume(co)",
        "future-await-error.luau");
    ASSERT_TRUE(script);
    ASSERT_TRUE(script.value().execute());

    ASSERT_TRUE(promise.set_error(lunaris::Error(lunaris::ErrorType::Runtime,
                                                 "future operation failed")));
    auto resumed = state.resume_ready();
    ASSERT_FALSE(resumed);
    EXPECT_EQ(resumed.error().type(), lunaris::ErrorType::Runtime);
    EXPECT_NE(resumed.error().message().find("future operation failed"),
              std::string_view::npos);
}

TEST(FutureTests, LuauAwaitPropagatesCancellation) {
    lunaris::State state;
    state.open_coroutine_library();
    lunaris::Promise<lunaris::Value> promise;
    state.set_future_global("cancelledFuture", promise.get_future());
    auto script = state.load_script(
        "local co = coroutine.create(function() "
        "cancelledFuture:await() end); return coroutine.resume(co)",
        "future-await-cancelled.luau");
    ASSERT_TRUE(script);
    ASSERT_TRUE(script.value().execute());

    ASSERT_TRUE(promise.cancel());
    auto resumed = state.resume_ready();
    ASSERT_FALSE(resumed);
    EXPECT_EQ(resumed.error().type(), lunaris::ErrorType::Runtime);
    EXPECT_NE(resumed.error().message().find("Future was cancelled"),
              std::string_view::npos);
}

TEST(FutureTests, LuauFutureIsSingleConsumerAcrossAwaitCalls) {
    lunaris::State state;
    state.open_coroutine_library();
    lunaris::Promise<lunaris::Value> promise;
    state.set_future_global("pending", promise.get_future());
    auto script = state.load_script(
        "local co = coroutine.create(function() "
        "pending:await(); pending:await() end); return coroutine.resume(co)",
        "future-await-single-consumer.luau");
    ASSERT_TRUE(script);
    ASSERT_TRUE(script.value().execute());

    ASSERT_TRUE(promise.set_value(lunaris::Value{std::int64_t{1}}));
    auto resumed = state.resume_ready();
    ASSERT_FALSE(resumed);
    EXPECT_EQ(resumed.error().type(), lunaris::ErrorType::Runtime);
    EXPECT_NE(resumed.error().message().find("already consumed"),
              std::string_view::npos);
}

TEST(FutureTests, LuauAwaitRequiresYieldableCoroutine) {
    lunaris::State state;
    lunaris::Promise<lunaris::Value> promise;
    state.set_future_global("pending", promise.get_future());
    auto script =
        state.load_script("return pending:await()", "future-await-main.luau");
    ASSERT_TRUE(script);
    auto result = script.value().execute();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Runtime);
}

TEST(FutureTests, LuauFutureGlobalValidatesNameAndFuture) {
    lunaris::State state;
    lunaris::Promise<lunaris::Value> promise;
    auto future = promise.get_future();

    EXPECT_THROW(state.set_future_global("", std::move(future)),
                 std::invalid_argument);

    lunaris::Promise<lunaris::Value> secondPromise;
    auto secondFuture = secondPromise.get_future();
    ASSERT_TRUE(state.set_global("existing", lunaris::Value{true}));
    EXPECT_THROW(state.set_future_global("existing", std::move(secondFuture)),
                 std::invalid_argument);
}

TEST(FutureTests, CoroutineLibraryDoesNotReplaceExistingGlobal) {
    lunaris::State state;
    ASSERT_TRUE(state.set_global("coroutine", lunaris::Value{true}));
    EXPECT_THROW(state.open_coroutine_library(), std::invalid_argument);
    auto value = state.get_global("coroutine");
    ASSERT_TRUE(value);
    EXPECT_TRUE(std::get<bool>(value.value()));
}

TEST(FutureTests, AwaitingFutureDoesNotKeepClosedStateAlive) {
    lunaris::Promise<lunaris::Value> promise;
    {
        lunaris::State state;
        state.open_coroutine_library();
        state.set_future_global("pending", promise.get_future());
        auto script = state.load_script(
            "local co = coroutine.create(function() pending:await() end); "
            "return coroutine.resume(co)",
            "future-await-state-closed.luau");
        ASSERT_TRUE(script);
        ASSERT_TRUE(script.value().execute());
    }

    ASSERT_TRUE(promise.set_value(lunaris::Value{std::int64_t{1}}));
}
