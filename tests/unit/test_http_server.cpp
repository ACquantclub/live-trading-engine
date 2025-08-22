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
    // in roughly 100ms (2 batches) plus some overhead. Total time should be less than 500ms
    EXPECT_LT(duration.count(), 500);

    // But it should take at least the processing time for 2 batches (since we have 16 requests
    // and 8 threads, each request takes 50ms) - allowing for some variance
    EXPECT_GT(duration.count(), 60);
}

TEST_F(HttpServerTest, NewRoutingSystemBasicRoutes) {
    // Test the new registerRoute method with basic routes
    server_->registerRoute("GET", "/api/test", [](const HttpRequest& req) -> HttpResponse {
        (void)req;
        HttpResponse resp;
        resp.status_code = 200;
        resp.body = R"({"message": "test endpoint"})";
        resp.headers["Content-Type"] = "application/json";
        return resp;
    });

    server_->registerRoute("POST", "/api/create", [](const HttpRequest& req) -> HttpResponse {
        (void)req;
        HttpResponse resp;
        resp.status_code = 201;
        resp.body = R"({"message": "created"})";
        resp.headers["Content-Type"] = "application/json";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test GET route
    std::string get_request =
        "GET /api/test HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string get_response = sendHttpRequest(get_request);
    EXPECT_NE(get_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(get_response.find(R"({"message": "test endpoint"})"), std::string::npos);

    // Test POST route
    std::string post_request =
        "POST /api/create HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string post_response = sendHttpRequest(post_request);
    EXPECT_NE(post_response.find("HTTP/1.1 201 Created"), std::string::npos);
    EXPECT_NE(post_response.find(R"({"message": "created"})"), std::string::npos);
}

TEST_F(HttpServerTest, ParameterizedRoutes) {
    // Test parameterized routes like /api/users/{userId}
    server_->registerRoute(
        "GET", "/api/users/{userId}", [](const HttpRequest& req) -> HttpResponse {
            HttpResponse resp;
            resp.status_code = 200;

            auto it = req.path_params.find("userId");
            if (it != req.path_params.end()) {
                resp.body = R"({"userId": ")" + it->second + R"(", "name": "Test User"})";
            } else {
                resp.body = R"({"error": "userId not found"})";
            }
            resp.headers["Content-Type"] = "application/json";
            return resp;
        });

    server_->registerRoute(
        "GET", "/api/orders/{orderId}/details", [](const HttpRequest& req) -> HttpResponse {
            HttpResponse resp;
            resp.status_code = 200;

            auto it = req.path_params.find("orderId");
            if (it != req.path_params.end()) {
                resp.body = R"({"orderId": ")" + it->second + R"(", "status": "filled"})";
            } else {
                resp.body = R"({"error": "orderId not found"})";
            }
            resp.headers["Content-Type"] = "application/json";
            return resp;
        });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test single parameter route
    std::string user_request =
        "GET /api/users/12345 HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string user_response = sendHttpRequest(user_request);
    EXPECT_NE(user_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(user_response.find(R"({"userId": "12345", "name": "Test User"})"), std::string::npos);

    // Test route with parameter in middle of path
    std::string order_request =
        "GET /api/orders/ORD-999/details HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string order_response = sendHttpRequest(order_request);
    EXPECT_NE(order_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(order_response.find(R"({"orderId": "ORD-999", "status": "filled"})"),
              std::string::npos);
}

TEST_F(HttpServerTest, MultipleParametersRoute) {
    // Test route with multiple parameters like /api/users/{userId}/orders/{orderId}
    server_->registerRoute(
        "GET", "/api/users/{userId}/orders/{orderId}", [](const HttpRequest& req) -> HttpResponse {
            HttpResponse resp;
            resp.status_code = 200;

            auto user_it = req.path_params.find("userId");
            auto order_it = req.path_params.find("orderId");

            if (user_it != req.path_params.end() && order_it != req.path_params.end()) {
                resp.body = R"({"userId": ")" + user_it->second + R"(", "orderId": ")" +
                            order_it->second + R"(", "found": true})";
            } else {
                resp.body = R"({"error": "parameters not found"})";
            }
            resp.headers["Content-Type"] = "application/json";
            return resp;
        });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string request =
        "GET /api/users/trader-123/orders/ORD-456 HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response = sendHttpRequest(request);
    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find(R"({"userId": "trader-123", "orderId": "ORD-456", "found": true})"),
              std::string::npos);
}

TEST_F(HttpServerTest, FastPathOptimization) {
    // Test that the fast path optimization works for /order and /health endpoints
    int order_call_count = 0;
    int health_call_count = 0;

    server_->registerRoute("POST", "/order",
                           [&order_call_count](const HttpRequest& req) -> HttpResponse {
                               (void)req;
                               order_call_count++;
                               HttpResponse resp;
                               resp.status_code = 202;
                               resp.body = R"({"status": "accepted"})";
                               resp.headers["Content-Type"] = "application/json";
                               return resp;
                           });

    server_->registerRoute("GET", "/health",
                           [&health_call_count](const HttpRequest& req) -> HttpResponse {
                               (void)req;
                               health_call_count++;
                               HttpResponse resp;
                               resp.status_code = 200;
                               resp.body = R"({"status": "healthy"})";
                               resp.headers["Content-Type"] = "application/json";
                               return resp;
                           });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test /order endpoint (fast path)
    std::string order_request =
        "POST /order HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n{}";

    std::string order_response = sendHttpRequest(order_request);
    EXPECT_NE(order_response.find("HTTP/1.1 202 Accepted"), std::string::npos);
    EXPECT_NE(order_response.find(R"({"status": "accepted"})"), std::string::npos);
    EXPECT_EQ(order_call_count, 1);

    // Test /health endpoint (fast path)
    std::string health_request =
        "GET /health HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string health_response = sendHttpRequest(health_request);
    EXPECT_NE(health_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(health_response.find(R"({"status": "healthy"})"), std::string::npos);
    EXPECT_EQ(health_call_count, 1);
}

TEST_F(HttpServerTest, RouteNotFound) {
    // Test that unregistered routes return 404
    server_->registerRoute("GET", "/api/existing", [](const HttpRequest& req) -> HttpResponse {
        (void)req;
        HttpResponse resp;
        resp.status_code = 200;
        resp.body = R"({"found": true})";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string request =
        "GET /api/nonexistent HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response = sendHttpRequest(request);
    EXPECT_NE(response.find("HTTP/1.1 404 Not Found"), std::string::npos);
    EXPECT_NE(response.find(R"({"error": "Not Found"})"), std::string::npos);
}

TEST_F(HttpServerTest, MethodNotAllowed) {
    // Test that wrong HTTP methods don't match
    server_->registerRoute("GET", "/api/getonly", [](const HttpRequest& req) -> HttpResponse {
        (void)req;
        HttpResponse resp;
        resp.status_code = 200;
        resp.body = R"({"method": "GET"})";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Try POST on a GET-only route
    std::string request =
        "POST /api/getonly HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response = sendHttpRequest(request);
    EXPECT_NE(response.find("HTTP/1.1 404 Not Found"), std::string::npos);
}

TEST_F(HttpServerTest, WildcardMethodRoute) {
    // Test wildcard method matching (method = "*")
    server_->registerRoute("*", "/api/any-method", [](const HttpRequest& req) -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 200;
        resp.body = R"({"method": ")" + req.method + R"("})";
        resp.headers["Content-Type"] = "application/json";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test GET
    std::string get_request =
        "GET /api/any-method HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string get_response = sendHttpRequest(get_request);
    EXPECT_NE(get_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(get_response.find(R"({"method": "GET"})"), std::string::npos);

    // Test POST
    std::string post_request =
        "POST /api/any-method HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string post_response = sendHttpRequest(post_request);
    EXPECT_NE(post_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(post_response.find(R"({"method": "POST"})"), std::string::npos);

    // Test PUT
    std::string put_request =
        "PUT /api/any-method HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string put_response = sendHttpRequest(put_request);
    EXPECT_NE(put_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(put_response.find(R"({"method": "PUT"})"), std::string::npos);
}

TEST_F(HttpServerTest, ParameterizedRoutesWithSpecialCharacters) {
    // Test that parameter values can contain various characters
    server_->registerRoute(
        "GET", "/api/symbols/{symbol}/price", [](const HttpRequest& req) -> HttpResponse {
            HttpResponse resp;
            resp.status_code = 200;

            auto it = req.path_params.find("symbol");
            if (it != req.path_params.end()) {
                resp.body = R"({"symbol": ")" + it->second + R"(", "price": 150.50})";
            } else {
                resp.body = R"({"error": "symbol not found"})";
            }
            resp.headers["Content-Type"] = "application/json";
            return resp;
        });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test with hyphenated symbol
    std::string request1 =
        "GET /api/symbols/BTC-USD/price HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response1 = sendHttpRequest(request1);
    EXPECT_NE(response1.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response1.find(R"({"symbol": "BTC-USD", "price": 150.50})"), std::string::npos);

    // Test with underscore
    std::string request2 =
        "GET /api/symbols/ETH_USD/price HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response2 = sendHttpRequest(request2);
    EXPECT_NE(response2.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response2.find(R"({"symbol": "ETH_USD", "price": 150.50})"), std::string::npos);
}

TEST_F(HttpServerTest, LegacyHandlersStillWork) {
    // Test that the legacy setOrderHandler and setHealthHandler still work
    // alongside the new routing system

    // Add a new route
    server_->registerRoute("GET", "/api/new-endpoint", [](const HttpRequest& req) -> HttpResponse {
        (void)req;
        HttpResponse resp;
        resp.status_code = 200;
        resp.body = R"({"message": "new routing system"})";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test legacy health handler
    std::string health_request =
        "GET /health HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string health_response = sendHttpRequest(health_request);
    EXPECT_NE(health_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(health_response.find(R"({"status": "healthy", "running": true})"), std::string::npos);

    // Test legacy order handler (/orders - note the 's')
    std::string order_request =
        "POST /orders HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n{}";

    std::string order_response = sendHttpRequest(order_request);
    EXPECT_NE(order_response.find("HTTP/1.1 202 Accepted"), std::string::npos);
    EXPECT_NE(order_response.find(R"({"message": "Order received", "id": "test-order"})"),
              std::string::npos);

    // Test new route
    std::string new_request =
        "GET /api/new-endpoint HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string new_response = sendHttpRequest(new_request);
    EXPECT_NE(new_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(new_response.find(R"({"message": "new routing system"})"), std::string::npos);
}

TEST_F(HttpServerTest, QueryParameterParsing) {
    // Test that query parameters are correctly parsed and accessible
    server_->registerRoute("GET", "/api/search", [](const HttpRequest& req) -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 200;

        std::string query_json = R"({"query_params": {)";
        bool first = true;
        for (const auto& [key, value] : req.query_params) {
            if (!first)
                query_json += ", ";
            query_json += R"(")" + key + R"(": ")" + value + R"(")";
            first = false;
        }
        query_json += "}}";

        resp.body = query_json;
        resp.headers["Content-Type"] = "application/json";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test single query parameter
    std::string request1 =
        "GET /api/search?symbol=AAPL HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response1 = sendHttpRequest(request1);
    EXPECT_NE(response1.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response1.find(R"("symbol": "AAPL")"), std::string::npos);

    // Test multiple query parameters
    std::string request2 =
        "GET /api/search?symbol=MSFT&limit=10&offset=20 HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response2 = sendHttpRequest(request2);
    EXPECT_NE(response2.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response2.find(R"("symbol": "MSFT")"), std::string::npos);
    EXPECT_NE(response2.find(R"("limit": "10")"), std::string::npos);
    EXPECT_NE(response2.find(R"("offset": "20")"), std::string::npos);
}

TEST_F(HttpServerTest, QueryParameterURLDecoding) {
    // Test that URL-encoded query parameters are properly decoded
    server_->registerRoute("GET", "/api/decode", [](const HttpRequest& req) -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 200;

        auto it = req.query_params.find("message");
        if (it != req.query_params.end()) {
            resp.body = R"({"decoded_message": ")" + it->second + R"("})";
        } else {
            resp.body = R"({"error": "no message parameter"})";
        }
        resp.headers["Content-Type"] = "application/json";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test URL decoding of spaces (+ and %20)
    std::string request1 =
        "GET /api/decode?message=hello+world HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response1 = sendHttpRequest(request1);
    EXPECT_NE(response1.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response1.find(R"("decoded_message": "hello world")"), std::string::npos);

    // Test URL decoding of %20 for spaces
    std::string request2 =
        "GET /api/decode?message=hello%20world HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response2 = sendHttpRequest(request2);
    EXPECT_NE(response2.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response2.find(R"("decoded_message": "hello world")"), std::string::npos);

    // Test URL decoding of special characters
    std::string request3 =
        "GET /api/decode?message=test%21%40%23 HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response3 = sendHttpRequest(request3);
    EXPECT_NE(response3.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response3.find(R"("decoded_message": "test!@#")"), std::string::npos);
}

TEST_F(HttpServerTest, QueryParametersWithoutValues) {
    // Test query parameters without values (e.g., ?debug&verbose)
    server_->registerRoute("GET", "/api/flags", [](const HttpRequest& req) -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 200;

        bool has_debug = req.query_params.find("debug") != req.query_params.end();
        bool has_verbose = req.query_params.find("verbose") != req.query_params.end();

        resp.body = R"({"debug": )" + std::string(has_debug ? "true" : "false") +
                    R"(, "verbose": )" + std::string(has_verbose ? "true" : "false") + "}";
        resp.headers["Content-Type"] = "application/json";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test parameters without values
    std::string request =
        "GET /api/flags?debug&verbose HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response = sendHttpRequest(request);
    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find(R"("debug": true)"), std::string::npos);
    EXPECT_NE(response.find(R"("verbose": true)"), std::string::npos);
}

TEST_F(HttpServerTest, QueryParametersWithPathParameters) {
    // Test that query parameters work alongside path parameters
    server_->registerRoute(
        "GET", "/api/users/{userId}/orders", [](const HttpRequest& req) -> HttpResponse {
            HttpResponse resp;
            resp.status_code = 200;

            auto user_it = req.path_params.find("userId");
            auto limit_it = req.query_params.find("limit");
            auto status_it = req.query_params.find("status");

            resp.body = R"({"userId": ")" +
                        (user_it != req.path_params.end() ? user_it->second : "unknown") + R"(")";

            if (limit_it != req.query_params.end()) {
                resp.body += R"(, "limit": ")" + limit_it->second + R"(")";
            }

            if (status_it != req.query_params.end()) {
                resp.body += R"(, "status": ")" + status_it->second + R"(")";
            }

            resp.body += "}";
            resp.headers["Content-Type"] = "application/json";
            return resp;
        });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test path parameter with query parameters
    std::string request =
        "GET /api/users/trader123/orders?limit=50&status=filled HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response = sendHttpRequest(request);
    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find(R"("userId": "trader123")"), std::string::npos);
    EXPECT_NE(response.find(R"("limit": "50")"), std::string::npos);
    EXPECT_NE(response.find(R"("status": "filled")"), std::string::npos);
}

TEST_F(HttpServerTest, EmptyQueryParameters) {
    // Test routes with no query parameters
    server_->registerRoute("GET", "/api/empty", [](const HttpRequest& req) -> HttpResponse {
        HttpResponse resp;
        resp.status_code = 200;

        resp.body = R"({"query_count": )" + std::to_string(req.query_params.size()) + "}";
        resp.headers["Content-Type"] = "application/json";
        return resp;
    });

    ASSERT_TRUE(server_->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test request without query parameters
    std::string request =
        "GET /api/empty HTTP/1.1\r\n"
        "Host: 127.0.0.1:8081\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::string response = sendHttpRequest(request);
    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find(R"("query_count": 0)"), std::string::npos);
}
