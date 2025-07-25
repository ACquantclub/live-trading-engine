#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <librdkafka/rdkafkacpp.h>

namespace trading {
namespace messaging {

struct Message {
    std::string topic;
    std::string key;
    std::string value;
    uint64_t timestamp;
    std::map<std::string, std::string> headers;
};

class QueueClient {
  public:
    using MessageHandler = std::function<void(const Message&)>;

    QueueClient(const std::string& brokers);
    ~QueueClient();

    // Connection management
    bool connect();
    void disconnect();
    bool isConnected() const;

    // Publishing
    bool publish(const Message& message);
    bool publish(const std::string& topic, const std::string& key, const std::string& value);

    // Subscription
    bool subscribe(const std::string& topic, MessageHandler handler);
    bool unsubscribe(const std::string& topic);

    // Configuration
    void setTimeout(int milliseconds);
    void setBatchSize(int batch_size);

  private:
    std::string brokers_;
    bool connected_;
    int timeout_ms_;
    int batch_size_;

    std::map<std::string, MessageHandler> topic_handlers_;

    std::unique_ptr<RdKafka::Producer> producer_;
    std::unique_ptr<RdKafka::KafkaConsumer> consumer_;
    std::atomic<bool> running_;
    std::thread message_thread_;

    void processMessages();
    bool validateTopic(const std::string& topic) const;
    bool validateBrokerAddress(const std::string& brokers) const;
    bool isValidIpAddress(const std::string& ip) const;
};

}  // namespace messaging
}  // namespace trading