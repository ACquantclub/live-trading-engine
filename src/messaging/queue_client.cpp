#include "trading/messaging/queue_client.hpp"
#include <chrono>
#include <iostream>
#include <sstream>

namespace trading {
namespace messaging {

QueueClient::QueueClient(const std::string& brokers)
    : brokers_(brokers), connected_(false), timeout_ms_(5000), batch_size_(100) {
}

QueueClient::~QueueClient() {
    if (connected_) {
        disconnect();
    }
}

bool QueueClient::connect() {
    std::string errstr;

    // Check for valid broker address
    if (!validateBrokerAddress(brokers_)) {
        std::cerr << "Invalid broker address: " << brokers_ << std::endl;
        return false;
    }

    // Create producer configuration
    auto producer_conf =
        std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    if (producer_conf->set("bootstrap.servers", brokers_, errstr) != RdKafka::Conf::CONF_OK) {
        std::cerr << "Failed to set bootstrap servers for producer: " << errstr << std::endl;
        return false;
    }

    // Create producer
    producer_ =
        std::unique_ptr<RdKafka::Producer>(RdKafka::Producer::create(producer_conf.get(), errstr));
    if (!producer_) {
        std::cerr << "Failed to create producer: " << errstr << std::endl;
        return false;
    }

    // Create consumer configuration
    auto consumer_conf =
        std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    if (consumer_conf->set("bootstrap.servers", brokers_, errstr) != RdKafka::Conf::CONF_OK) {
        std::cerr << "Failed to set bootstrap servers for consumer: " << errstr << std::endl;
        return false;
    }
    if (consumer_conf->set("group.id", "trading-engine-consumers", errstr) !=
        RdKafka::Conf::CONF_OK) {
        std::cerr << "Failed to set group.id: " << errstr << std::endl;
        return false;
    }
    if (consumer_conf->set("auto.offset.reset", "earliest", errstr) != RdKafka::Conf::CONF_OK) {
        std::cerr << "Failed to set auto.offset.reset: " << errstr << std::endl;
        return false;
    }

    // Create KafkaConsumer (high-level consumer)
    consumer_ = std::unique_ptr<RdKafka::KafkaConsumer>(
        RdKafka::KafkaConsumer::create(consumer_conf.get(), errstr));
    if (!consumer_) {
        std::cerr << "Failed to create consumer: " << errstr << std::endl;
        return false;
    }

    connected_ = true;
    running_ = true;
    message_thread_ = std::thread(&QueueClient::processMessages, this);
    return true;
}

bool QueueClient::validateBrokerAddress(const std::string& brokers) const {
    // Check if broker address is non-empty
    if (brokers.empty()) {
        std::cerr << "No broker address provided" << std::endl;
        return false;
    }

    std::istringstream ss(brokers);
    std::string broker;
    bool valid = false;

    // Parse comma-separated broker list
    while (std::getline(ss, broker, ',')) {
        // Remove leading/trailing whitespace
        broker.erase(0, broker.find_first_not_of(" \t"));
        broker.erase(broker.find_last_not_of(" \t") + 1);

        // Check if broker address is in the format "host:port"
        size_t colon_pos = broker.find(':');
        if (colon_pos == std::string::npos || colon_pos == 0 || colon_pos == broker.length() - 1) {
            std::cerr << "Invalid broker format: " << broker << ". Expected host:port" << std::endl;
            return false;
        }

        // Split into host and port
        std::string host = broker.substr(0, colon_pos);
        std::string port = broker.substr(colon_pos + 1);

        // Check if port is a valid number
        try {
            int port_num = std::stoi(port);
            if (port_num <= 0 || port_num > 65535) {
                std::cerr << "Invalid port number: " << port << ". Must be between 1 and 65535"
                          << std::endl;
                return false;
            }
            valid = true;
        } catch (const std::exception&) {
            std::cerr << "Invalid port number: " << port << std::endl;
            return false;
        }

        // Enhanced host validation
        if (host.empty()) {
            std::cerr << "Empty host name" << std::endl;
            return false;
        }

        // Only allow localhost or IP address format
        if (host != "localhost" && !isValidIpAddress(host)) {
            std::cerr << "Invalid host: " << host << ". Must be 'localhost' or valid IP address"
                      << std::endl;
            return false;
        }
    }

    if (!valid) {
        std::cerr << "No valid broker addresses found" << std::endl;
        return false;
    }

    return true;
}

void QueueClient::disconnect() {
    running_ = false;

    if (message_thread_.joinable()) {
        message_thread_.join();
    }

    if (consumer_) {
        consumer_->close();
    }

    producer_.reset();
    consumer_.reset();

    connected_ = false;
}

bool QueueClient::isConnected() const {
    return connected_;
}

bool QueueClient::publish(const Message& message) {
    if (!connected_ || !producer_) {
        return false;
    }

    // Use the modern produce API that takes topic name as string
    RdKafka::ErrorCode err =
        producer_->produce(message.topic,                             // topic name
                           RdKafka::Topic::PARTITION_UA,              // partition (unassigned)
                           RdKafka::Producer::RK_MSG_COPY,            // message flags
                           const_cast<char*>(message.value.c_str()),  // value payload
                           message.value.length(),                    // value length
                           message.key.empty() ? nullptr : message.key.c_str(),  // key
                           message.key.length(),                                 // key length
                           0,       // timestamp (0 = now)
                           nullptr  // opaque user data
        );

    if (err != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Failed to produce message: " << RdKafka::err2str(err) << std::endl;
        return false;
    }

    // Poll to handle delivery reports
    producer_->poll(0);
    return true;
}

bool QueueClient::publish(const std::string& topic, const std::string& key,
                          const std::string& value) {
    Message msg;
    msg.topic = topic;
    msg.key = key;
    msg.value = value;
    msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    return publish(msg);
}

bool QueueClient::subscribe(const std::string& topic, MessageHandler handler) {
    if (!connected_ || !consumer_) {
        return false;
    }

    topic_handlers_[topic] = handler;

    // Build list of all topics to subscribe to
    std::vector<std::string> topics;
    for (const auto& pair : topic_handlers_) {
        topics.push_back(pair.first);
    }

    // Subscribe to topics using KafkaConsumer
    RdKafka::ErrorCode err = consumer_->subscribe(topics);
    if (err != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Failed to subscribe to topics: " << RdKafka::err2str(err) << std::endl;
        return false;
    }

    return true;
}

bool QueueClient::unsubscribe(const std::string& topic) {
    if (topic_handlers_.erase(topic) == 0) {
        return false;
    }

    if (!connected_ || !consumer_) {
        return true;
    }

    if (topic_handlers_.empty()) {
        // Unsubscribe from all topics
        RdKafka::ErrorCode err = consumer_->unsubscribe();
        if (err != RdKafka::ERR_NO_ERROR) {
            std::cerr << "Failed to unsubscribe from all topics: " << RdKafka::err2str(err)
                      << std::endl;
            return false;
        }
    } else {
        // Resubscribe to remaining topics
        std::vector<std::string> topics;
        for (const auto& pair : topic_handlers_) {
            topics.push_back(pair.first);
        }
        RdKafka::ErrorCode err = consumer_->subscribe(topics);
        if (err != RdKafka::ERR_NO_ERROR) {
            std::cerr << "Failed to resubscribe to topics: " << RdKafka::err2str(err) << std::endl;
            return false;
        }
    }

    return true;
}

void QueueClient::setTimeout(int milliseconds) {
    timeout_ms_ = milliseconds;
}

void QueueClient::setBatchSize(int batch_size) {
    batch_size_ = batch_size;
}

void QueueClient::processMessages() {
    while (running_) {
        auto kafka_msg = std::unique_ptr<RdKafka::Message>(consumer_->consume(timeout_ms_));

        switch (kafka_msg->err()) {
            case RdKafka::ERR_NO_ERROR: {
                // Create our message
                Message msg;
                msg.topic = kafka_msg->topic_name();

                // Set key if available
                if (kafka_msg->key()) {
                    msg.key = *kafka_msg->key();
                }

                // Set value
                msg.value = std::string(static_cast<char*>(kafka_msg->payload()), kafka_msg->len());

                // Set timestamp
                msg.timestamp = kafka_msg->timestamp().timestamp;

                // Find topic handler and call it
                auto it = topic_handlers_.find(msg.topic);
                if (it != topic_handlers_.end()) {
                    it->second(msg);
                }
                break;
            }
            case RdKafka::ERR__TIMED_OUT:
                // Normal timeout, continue
                break;
            case RdKafka::ERR__PARTITION_EOF:
                // End of partition, continue
                break;
            default:
                std::cerr << "Consumer error: " << kafka_msg->errstr() << std::endl;
                break;
        }
    }
}

bool QueueClient::validateTopic(const std::string& topic) const {
    return !topic.empty();
}

bool QueueClient::isValidIpAddress(const std::string& ip) const {
    std::istringstream ss(ip);
    std::string segment;
    int segments = 0;

    while (std::getline(ss, segment, '.')) {
        if (segments >= 4)
            return false;  // Too many segments

        // Check if segment is a valid number
        try {
            int value = std::stoi(segment);
            if (value < 0 || value > 255)
                return false;
            if (segment.length() > 1 && segment[0] == '0')
                return false;  // No leading zeros
        } catch (const std::exception&) {
            return false;
        }
        segments++;
    }

    return segments == 4;  // Must have exactly 4 segments
}

}  // namespace messaging
}  // namespace trading