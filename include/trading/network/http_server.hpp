#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "trading/utils/thread_pool.hpp"

namespace trading {
namespace network {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct HttpResponse {
    int status_code;
    std::string body;
    std::map<std::string, std::string> headers;
};

class HttpServer {
  public:
    using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer(const std::string& host, int port, int threads = 4);
    ~HttpServer();

    // Server lifecycle
    bool start();
    void stop();
    bool isRunning() const;

    // Route handling
    void setOrderHandler(RequestHandler handler);
    void setHealthHandler(RequestHandler handler);

    // Configuration
    void setTimeout(int seconds);
    void setMaxConnections(int max_connections);

  private:
    std::string host_;
    int port_;
    bool running_;
    int timeout_seconds_;
    int max_connections_;

    std::unique_ptr<utils::ThreadPool> thread_pool_;

    RequestHandler order_handler_;
    RequestHandler health_handler_;

    void handleRequest(const HttpRequest& request);
    void handleClientRequest(int client_fd);
    HttpResponse createErrorResponse(int status_code, const std::string& message);

    // Internal server state
    int server_fd_ = -1;
    std::thread server_thread_;
    std::atomic<bool> stop_flag_{false};
};

}  // namespace network
}  // namespace trading