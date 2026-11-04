#include "lunaris/MessageQueue.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

TEST(MessageQueueTests, DeliversMessagesInFifoOrder) {
    lunaris::MessageQueue queue;
    ASSERT_TRUE(queue.send("Update", lunaris::Value{std::int64_t{1}}));
    ASSERT_TRUE(queue.send("Spawn", lunaris::Value{std::string{"enemy"}}));

    ASSERT_EQ(queue.size(), 2U);
    auto first = queue.try_receive();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->topic, "Update");
    EXPECT_EQ(std::get<std::int64_t>(first->payload), 1);

    auto second = queue.receive_for(std::chrono::milliseconds(0));
    ASSERT_TRUE(second);
    EXPECT_EQ(second->topic, "Spawn");
    EXPECT_EQ(std::get<std::string>(second->payload), "enemy");
    EXPECT_FALSE(queue.try_receive());
}

TEST(MessageQueueTests, PreservesNilAndValidatesTopics) {
    lunaris::MessageQueue queue;
    ASSERT_TRUE(queue.send("Clear", lunaris::Value{std::monostate{}}));
    auto message = queue.try_receive();
    ASSERT_TRUE(message);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(message->payload));

    EXPECT_THROW((void)queue.send("", lunaris::Value{}), std::invalid_argument);
    EXPECT_THROW(
        (void)queue.send(std::string_view{"a\0b", 3}, lunaris::Value{}),
        std::invalid_argument);
}

TEST(MessageQueueTests, CloseRejectsSendsAndDrainsQueuedMessages) {
    lunaris::MessageQueue queue;
    ASSERT_TRUE(queue.send("First", lunaris::Value{true}));
    ASSERT_TRUE(queue.send("Second", lunaris::Value{false}));

    queue.close();
    queue.close();
    EXPECT_TRUE(queue.is_closed());
    EXPECT_FALSE(queue.send("AfterClose", lunaris::Value{}));

    auto first = queue.receive();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->topic, "First");
    auto second = queue.receive();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->topic, "Second");
    EXPECT_FALSE(queue.receive());
    EXPECT_FALSE(queue.receive_for(std::chrono::milliseconds(0)));
}

TEST(MessageQueueTests, ReceiveForReturnsWhenQueueIsEmpty) {
    lunaris::MessageQueue queue;
    EXPECT_FALSE(queue.receive_for(std::chrono::milliseconds(0)));
    EXPECT_FALSE(queue.is_closed());
}

TEST(MessageQueueTests, CloseWakesWaitingReceiver) {
    lunaris::MessageQueue queue;
    std::barrier receiverStarted(2);
    std::optional<lunaris::Message> received;

    std::thread receiver([&] {
        receiverStarted.arrive_and_wait();
        received = queue.receive();
    });
    receiverStarted.arrive_and_wait();
    queue.close();
    receiver.join();

    EXPECT_FALSE(received);
}

TEST(MessageQueueTests, ConcurrentProducersDoNotLoseMessages) {
    lunaris::MessageQueue queue;
    constexpr int producerCount = 4;
    constexpr int messagesPerProducer = 100;
    std::barrier start(producerCount + 1);
    std::atomic<int> rejected = 0;
    std::vector<std::thread> producers;
    producers.reserve(producerCount);

    for (int producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([&, producer] {
            start.arrive_and_wait();
            for (int index = 0; index < messagesPerProducer; ++index) {
                if (!queue.send("Work",
                                lunaris::Value{static_cast<std::int64_t>(
                                    producer * messagesPerProducer + index)})) {
                    ++rejected;
                }
            }
        });
    }

    start.arrive_and_wait();
    for (auto &producer : producers) {
        producer.join();
    }

    EXPECT_EQ(rejected, 0);
    ASSERT_EQ(queue.size(), producerCount * messagesPerProducer);

    std::vector<bool> received(producerCount * messagesPerProducer, false);
    while (auto message = queue.try_receive()) {
        ASSERT_EQ(message->topic, "Work");
        const auto value = std::get<std::int64_t>(message->payload);
        ASSERT_GE(value, 0);
        ASSERT_LT(value, static_cast<std::int64_t>(received.size()));
        EXPECT_FALSE(received[static_cast<std::size_t>(value)]);
        received[static_cast<std::size_t>(value)] = true;
    }
    EXPECT_EQ(std::count(received.begin(), received.end(), true),
              producerCount * messagesPerProducer);
}
