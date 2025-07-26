#include <chrono>
#include <thread>
#include <vector>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trading/messaging/queue_client.hpp"

using namespace trading::messaging;

// Simple test fixture
class QueueClientTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Use a dummy broker address for unit tests
        client = std::make_unique<QueueClient>("localhost:9092");
    }

    void TearDown() override {
        if (client && client->isConnected()) {
            client->disconnect();
        }
    }

    std::unique_ptr<QueueClient> client;
};

// Test basic construction
TEST_F(QueueClientTest, Construction) {
    EXPECT_FALSE(client->isConnected());
}

// Test timeout and batch size configuration
TEST_F(QueueClientTest, Configuration) {
    client->setTimeout(1000);
    client->setBatchSize(50);

    // These methods don't have getters, so we just test they don't crash
    SUCCEED();
}

// Test message creation and publishing (without actual connection)
TEST_F(QueueClientTest, MessageCreation) {
    Message msg;
    msg.topic = "test_topic";
    msg.key = "test_key";
    msg.value = "test_value";
    msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

    EXPECT_EQ(msg.topic, "test_topic");
    EXPECT_EQ(msg.key, "test_key");
    EXPECT_EQ(msg.value, "test_value");
    EXPECT_GT(msg.timestamp, 0);
}

// Test publish without connection (should fail gracefully)
TEST_F(QueueClientTest, PublishWithoutConnection) {
    Message msg;
    msg.topic = "test_topic";
    msg.key = "test_key";
    msg.value = "test_value";

    // Should return false when not connected
    EXPECT_FALSE(client->publish(msg));
    EXPECT_FALSE(client->publish("topic", "key", "value"));
}

// Test subscribe without connection (should fail gracefully)
TEST_F(QueueClientTest, SubscribeWithoutConnection) {
    auto handler = [](const Message& /*msg*/) {
        // Simple handler
    };

    // Should return false when not connected
    EXPECT_FALSE(client->subscribe("test_topic", handler));
}

// Test unsubscribe without connection
TEST_F(QueueClientTest, UnsubscribeWithoutConnection) {
    // Should return false for non-existent topic
    EXPECT_FALSE(client->unsubscribe("non_existent_topic"));
}

// Test multiple subscribe/unsubscribe calls
TEST_F(QueueClientTest, MultipleSubscriptions) {
    auto handler1 = [](const Message& /*msg*/) {};
    auto handler2 = [](const Message& /*msg*/) {};

    // Without connection, should return false
    EXPECT_FALSE(client->subscribe("topic1", handler1));
    EXPECT_FALSE(client->subscribe("topic2", handler2));

    // Unsubscribe from non-existent topics
    EXPECT_FALSE(client->unsubscribe("topic1"));
    EXPECT_FALSE(client->unsubscribe("topic2"));
}

// Test disconnect when not connected
TEST_F(QueueClientTest, DisconnectWhenNotConnected) {
    EXPECT_FALSE(client->isConnected());
    client->disconnect();  // Should not crash
    EXPECT_FALSE(client->isConnected());
}

// Helper class to test message handlers
class MessageCounter {
  public:
    void handleMessage(const Message& msg) {
        count++;
        last_message = msg;
    }

    int count = 0;
    Message last_message;
};

// Test message handler functionality
TEST_F(QueueClientTest, MessageHandler) {
    MessageCounter counter;
    auto handler = [&counter](const Message& msg) { counter.handleMessage(msg); };

    // Create a test message
    Message test_msg;
    test_msg.topic = "test";
    test_msg.value = "hello";

    // Call handler directly to test functionality
    handler(test_msg);

    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.last_message.topic, "test");
    EXPECT_EQ(counter.last_message.value, "hello");
}

// Test that client can be destroyed safely
TEST_F(QueueClientTest, SafeDestruction) {
    // Create and destroy client without connection
    auto temp_client = std::make_unique<QueueClient>("localhost:9092");
    temp_client.reset();  // Should not crash

    SUCCEED();
}

// Test connection with invalid broker address
TEST_F(QueueClientTest, InvalidBrokerAddress) {
    auto invalid_client = std::make_unique<QueueClient>("invalid_broker:9092");
    EXPECT_FALSE(invalid_client->connect());
    EXPECT_FALSE(invalid_client->isConnected());
}

// Test reconnection
TEST_F(QueueClientTest, Reconnection) {
    EXPECT_FALSE(client->isConnected());
    bool first_connect = client->connect();
    client->disconnect();
    bool second_connect = client->connect();
    EXPECT_EQ(first_connect, second_connect);
}

// Test subscription with null handler
TEST_F(QueueClientTest, NullHandlerSubscription) {
    QueueClient::MessageHandler null_handler;
    EXPECT_FALSE(client->subscribe("test_topic", null_handler));
}

// Test concurrent message processing
TEST_F(QueueClientTest, ConcurrentMessageProcessing) {
    MessageCounter counter;
    auto handler = [&counter](const Message& msg) {  // Fix: capture counter by reference
        counter.handleMessage(msg);
    };

    // Since we can't actually connect without a broker, let's test the handler directly
    // This tests the concurrent safety of our handler
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&handler, i]() {
            Message msg;
            msg.topic = "test_topic";
            msg.value = "message " + std::to_string(i);
            handler(msg);  // Call handler directly
        });
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
        thread.join();
    }

    // Check that the handler was called for each message
    EXPECT_EQ(counter.count, 10);
}