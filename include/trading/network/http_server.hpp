#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

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

    HttpServer(const std::string& host, int port);
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

    RequestHandler order_handler_;
    RequestHandler health_handler_;

    void handleRequest(const HttpRequest& request);
    HttpResponse createErrorResponse(int status_code, const std::string& message);
};

}  // namespace network
}  // namespace trading