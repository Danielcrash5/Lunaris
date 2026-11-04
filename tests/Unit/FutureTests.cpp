#include "lunaris/Future.h"

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
