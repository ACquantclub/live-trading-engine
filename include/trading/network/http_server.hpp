#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "trading/utils/thread_pool.hpp"

namespace trading {
namespace network {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> path_params;
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

    // New flexible route registration
    void registerRoute(const std::string& method, const std::string& path_pattern,
                       RequestHandler handler);

    // Legacy route handling (for backward compatibility)
    void setOrderHandler(RequestHandler handler);
    void setHealthHandler(RequestHandler handler);

    // Configuration
    void setTimeout(int seconds);
    void setMaxConnections(int max_connections);

  private:
    struct Route {
        std::string method;
        std::string path_pattern;
        std::regex path_regex;
        std::vector<std::string> param_names;
        RequestHandler handler;
    };

    std::string host_;
    int port_;
    bool running_;
    int timeout_seconds_;
    int max_connections_;

    std::unique_ptr<utils::ThreadPool> thread_pool_;
    std::vector<Route> routes_;

    RequestHandler order_handler_;
    RequestHandler health_handler_;

    void handleRequest(const HttpRequest& request);
    void handleClientRequest(int client_fd);
    HttpResponse routeRequest(const HttpRequest& request);
    HttpResponse createErrorResponse(int status_code, const std::string& message);

    std::regex pathPatternToRegex(const std::string& pattern,
                                  std::vector<std::string>& param_names);

    // Internal server state
    int server_fd_ = -1;
    std::thread server_thread_;
    std::atomic<bool> stop_flag_{false};
};

}  // namespace network
}  // namespace trading