#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "trading/network/http_server.hpp"

using namespace trading::network;

class HttpServerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        server_ = std::make_unique<HttpServer>("127.0.0.1", 8081);

        // Set up handlers
        server_->setHealthHandler([](const HttpRequest& req) -> HttpResponse {
            (void)req;  // Suppress unused parameter warning
            HttpResponse resp;
            resp.status_code = 200;
            resp.body = R"({"status": "healthy", "running": true})";
            resp.headers["Content-Type"] = "application/json";
            return resp;
        });

        server_->setOrderHandler([](const HttpRequest& req) -> HttpResponse {
            (void)req;  // Suppress unused parameter warning
            HttpResponse resp;
            resp.status_code = 202;
            resp.body = R"({"message": "Order received", "id": "test-order"})";
            resp.headers["Content-Type"] = "application/json";
            return resp;
        });
    }

    void TearDown() override {
        if (server_ && server_->isRunning()) {
            server_->stop();
        }
        server_.reset();
    }

    // Helper function to send HTTP request via raw socket
    std::string sendHttpRequest(const std::string& request) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return "ERROR: Could not create socket";
        }

        struct sockaddr_in server_addr {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(8081);
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sock);
            return "ERROR: Could not connect";
        }

        // Send request
        send(sock, request.c_str(), request.length(), 0);

        // Read response
        std::string response;
        char buffer[4096];
        int bytes_received;
        while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes_received] = '\0';
            response += buffer;
            // Simple check to see if we got a complete HTTP response
            if (response.find("\r\n\r\n") != std::string::npos) {
                // Check if we need to read more based on Content-Length
                auto cl_pos = response.find("Content-Length:");
                if (cl_pos != std::string::npos) {
                    auto cl_end = response.find("\r\n", cl_pos);
                    if (cl_end != std::string::npos) {
                        std::string cl_str = response.substr(cl_pos + 15, cl_end - cl_pos - 15);
                        // Simple trim
                        while (!cl_str.empty() && cl_str[0] == ' ')
                            cl_str.erase(0, 1);
                        int content_length = std::stoi(cl_str);

                        auto body_start = response.find("\r\n\r\n") + 4;
                        int body_length = response.length() - body_start;

                        if (body_length >= content_length) {
                            break;
                        }
                    }
                }
            }
        }

        close(sock);
        return response;
    }

    std::unique_ptr<HttpServer> server_;
};

TEST_F(HttpServerTest, ServerStartAndStop) {
    EXPECT_FALSE(server_->isRunning());

    EXPECT_TRUE(server_->start());
    EXPECT_TRUE(server_->isRunning());

    // Give server a moment to fully start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server_->stop();
    EXPECT_FALSE(server_->isRunning());
}

TEST_F(HttpServerTest, HealthEndpoint) {
    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string request =
        "GET /health HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response = sendHttpRequest(request);

    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: application/json"), std::string::npos);
    EXPECT_NE(response.find(R"({"status": "healthy", "running": true})"), std::string::npos);
}

TEST_F(HttpServerTest, OrderEndpointWithContentLength) {
    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string json_body =
        R"({"id":"TEST_1","userId":"trader-1","symbol":"AAPL","type":"LIMIT","side":"BUY","quantity":1.0,"price":150.50})";
    std::string request =
        "POST /orders HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(json_body.length()) +
        "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        json_body;

    std::string response = sendHttpRequest(request);

    EXPECT_NE(response.find("HTTP/1.1 202 Accepted"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: application/json"), std::string::npos);
    EXPECT_NE(response.find(R"({"message": "Order received", "id": "test-order"})"),
              std::string::npos);
}

TEST_F(HttpServerTest, OrderEndpointWithoutContentLength) {
    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string json_body =
        R"({"id":"TEST_2","userId":"trader-2","symbol":"MSFT","type":"LIMIT","side":"SELL","quantity":2.0,"price":300.00})";
    std::string request =
        "POST /orders HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n" +
        json_body;

    std::string response = sendHttpRequest(request);

    // This test specifically checks that the server doesn't hang when no Content-Length is provided
    EXPECT_NE(response.find("HTTP/1.1 202 Accepted"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: application/json"), std::string::npos);
}

TEST_F(HttpServerTest, NotFoundEndpoint) {
    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string request =
        "GET /unknown HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response = sendHttpRequest(request);

    EXPECT_NE(response.find("HTTP/1.1 404 Not Found"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: application/json"), std::string::npos);
    EXPECT_NE(response.find(R"({"error": "Not Found"})"), std::string::npos);
}

TEST_F(HttpServerTest, MultipleSimultaneousRequests) {
    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const int num_requests = 5;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_requests; ++i) {
        threads.emplace_back([this, i, &success_count]() {
            std::string json_body =
                R"({"id":"TEST_)" + std::to_string(i) + R"(","userId":"trader-)" +
                std::to_string(i) +
                R"(","symbol":"AAPL","type":"LIMIT","side":"BUY","quantity":1.0,"price":150.50})";
            std::string request =
                "POST /orders HTTP/1.1\r\n"
                "Host: 127.0.0.1:8081\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " +
                std::to_string(json_body.length()) +
                "\r\n"
                "Connection: close\r\n"
                "\r\n" +
                json_body;

            std::string response = sendHttpRequest(request);
            if (response.find("HTTP/1.1 202 Accepted") != std::string::npos) {
                success_count++;
            }
        });
    }

    // Wait for all threads to complete with timeout
    for (auto& thread : threads) {
        thread.join();
    }

    // All requests should succeed
    EXPECT_EQ(success_count.load(), num_requests);
}

TEST_F(HttpServerTest, RequestTimeout) {
    // Set a short timeout
    server_->setTimeout(2);
    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create a socket but don't send the complete request
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    struct sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8081);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    ASSERT_EQ(connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)), 0);

    // Send incomplete request
    std::string incomplete_request =
        "POST /orders HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Length: 1000\r\n"
        "\r\n";
    send(sock, incomplete_request.c_str(), incomplete_request.length(), 0);

    // Wait longer than the timeout
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // The server should have closed the connection due to timeout
    char buffer[1];
    int result = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);
    EXPECT_EQ(result, 0);  // Connection should be closed

    close(sock);
}

TEST_F(HttpServerTest, LargeRequestBody) {
    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create a large JSON body (but reasonable size)
    std::string large_field(1000, 'A');  // 1KB of 'A' characters
    std::string json_body =
        R"({"id":"TEST_LARGE","userId":"trader-large","symbol":"AAPL","type":"LIMIT","side":"BUY","quantity":1.0,"price":150.50,"large_field":")" +
        large_field + R"("})";

    std::string request =
        "POST /orders HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(json_body.length()) +
        "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        json_body;

    std::string response = sendHttpRequest(request);

    EXPECT_NE(response.find("HTTP/1.1 202 Accepted"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: application/json"), std::string::npos);
}

TEST_F(HttpServerTest, ConfigurableThreadPoolHandlesConcurrentRequests) {
    // Test with a custom thread count to verify configurability
    const int custom_thread_count = 8;
    server_.reset();  // Reset the existing server
    server_ = std::make_unique<HttpServer>("127.0.0.1", 8081, custom_thread_count);

    // Set up handlers for the new server
    server_->setHealthHandler([](const HttpRequest& req) -> HttpResponse {
        (void)req;
        HttpResponse resp;
        resp.status_code = 200;
        resp.body = R"({"status": "healthy", "running": true})";
        resp.headers["Content-Type"] = "application/json";
        return resp;
    });

    server_->setOrderHandler([](const HttpRequest& req) -> HttpResponse {
        // Simulate some processing time to stress the thread pool
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        (void)req;
        HttpResponse resp;
        resp.status_code = 202;
        resp.body = R"({"message": "Order received", "id": "test-order"})";
        resp.headers["Content-Type"] = "application/json";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test with more concurrent requests than the default thread count would handle efficiently
    const int num_concurrent_requests = 16;  // 2x our custom thread count
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> started_count{0};

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < num_concurrent_requests; ++i) {
        threads.emplace_back([this, i, &success_count, &started_count]() {
            started_count++;
            std::string json_body =
                R"({"id":"CONCURRENT_)" + std::to_string(i) + R"(","userId":"trader-)" +
                std::to_string(i) +
                R"(","symbol":"AAPL","type":"LIMIT","side":"BUY","quantity":1.0,"price":150.50})";
            std::string request =
                "POST /orders HTTP/1.1\r\n"
                "Host: 127.0.0.1:8081\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " +
                std::to_string(json_body.length()) +
                "\r\n"
                "Connection: close\r\n"
                "\r\n" +
                json_body;

            std::string response = sendHttpRequest(request);
            if (response.find("HTTP/1.1 202 Accepted") != std::string::npos) {
                success_count++;
            }
        });
    }

    // Wait for all threads to complete with timeout
    for (auto& thread : threads) {
        thread.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // All requests should succeed
    EXPECT_EQ(success_count.load(), num_concurrent_requests);
    EXPECT_EQ(started_count.load(), num_concurrent_requests);

    // With 8 threads and 50ms processing time per request, 16 requests should complete
    // in roughly 100ms (2 batches) plus some overhead. Total time should be less than 500ms.
    EXPECT_LT(duration.count(), 500);

    // But it should take at least the processing time for 2 batches (since we have 16 requests
    // and 8 threads, each request takes 50ms) - allowing for some variance
    EXPECT_GT(duration.count(), 60);
}
