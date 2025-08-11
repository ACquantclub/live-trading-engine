#include "trading/network/http_server.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <sstream>

namespace trading {
namespace network {

namespace {
std::string reasonPhrase(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 202:
            return "Accepted";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
    }
}

static void setSocketTimeout(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
}  // namespace

HttpServer::HttpServer(const std::string& host, int port, int threads)
    : host_(host), port_(port), running_(false), timeout_seconds_(30), max_connections_(100) {
    // Initialize thread pool with configurable number of threads
    thread_pool_ = std::make_unique<utils::ThreadPool>(threads);
}

HttpServer::~HttpServer() {
    if (running_) {
        stop();
    }
}

bool HttpServer::start() {
    if (running_)
        return true;

    // Resolve host and bind
    struct addrinfo hints {};
    struct addrinfo* res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;  // for binding

    std::string port_str = std::to_string(port_);
    int rc =
        getaddrinfo(host_ == "0.0.0.0" ? nullptr : host_.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        return false;
    }

    server_fd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd_ < 0) {
        freeaddrinfo(res);
        return false;
    }

    int yes = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (::bind(server_fd_, res->ai_addr, res->ai_addrlen) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    if (::listen(server_fd_, max_connections_ > 0 ? max_connections_ : 100) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    stop_flag_ = false;
    running_ = true;

    server_thread_ = std::thread([this]() {
        setSocketTimeout(server_fd_, timeout_seconds_);
        while (!stop_flag_) {
            struct sockaddr_in client_addr {};
            socklen_t addr_len = sizeof(client_addr);
            int client_fd =
                ::accept(server_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
            if (client_fd < 0) {
                if (stop_flag_)
                    break;
                // EAGAIN/EINTR handled by continuing loop
                continue;
            }

            // Enqueue client handling to thread pool instead of processing synchronously
            thread_pool_->enqueue([this, client_fd]() { handleClientRequest(client_fd); });
        }
    });

    return true;
}

void HttpServer::handleClientRequest(int client_fd) {
    setSocketTimeout(client_fd, timeout_seconds_);

    // Read request into buffer
    std::string request_raw;
    char buf[4096];
    ssize_t n;
    bool headers_done = false;
    size_t content_length = 0;
    bool has_content_length = false;

    while ((n = ::recv(client_fd, buf, sizeof(buf), 0)) > 0) {
        request_raw.append(buf, buf + n);
        if (!headers_done) {
            auto pos = request_raw.find("\r\n\r\n");
            if (pos != std::string::npos) {
                headers_done = true;
                // Parse Content-Length if present
                std::string headers = request_raw.substr(0, pos + 4);
                std::istringstream hs(headers);
                std::string line;
                std::getline(hs, line);  // request line
                while (std::getline(hs, line)) {
                    if (line == "\r")
                        break;
                    auto colon = line.find(":");
                    if (colon != std::string::npos) {
                        std::string key = line.substr(0, colon);
                        std::string value = line.substr(colon + 1);
                        // trim
                        while (!value.empty() && (value.front() == ' '))
                            value.erase(value.begin());
                        if (!value.empty() && value.back() == '\r')
                            value.pop_back();
                        for (auto& c : key)
                            c = std::tolower(c);
                        if (key == "content-length") {
                            content_length = static_cast<size_t>(std::stoul(value));
                            has_content_length = true;
                        }
                    }
                }

                // Check if body already fully received
                size_t body_received = request_raw.size() - (pos + 4);
                if (has_content_length) {
                    while (body_received < content_length) {
                        n = ::recv(client_fd, buf, sizeof(buf), 0);
                        if (n <= 0) {
                            // Check if this was a timeout or error
                            if (n == 0) {
                                // Connection closed by client
                                break;
                            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                                                 errno == ETIMEDOUT)) {
                                // Timeout occurred - close connection and skip processing
                                ::close(client_fd);
                                n = -1;  // Mark for skipping
                                break;
                            } else {
                                // Other error - break and try to process what we have
                                break;
                            }
                        }
                        request_raw.append(buf, buf + n);
                        body_received += static_cast<size_t>(n);
                    }
                }
                break;  // we have headers + body (or as much as available)
            }
        } else {
            // If headers are done and we don't have content-length, assume body is complete
            if (!has_content_length) {
                break;
            }
        }
    }

    // Check if the main recv loop exited due to timeout or error
    if (n <= 0) {
        if (n == 0) {
            // Connection closed cleanly
            ::close(client_fd);
            return;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
            // Timeout occurred
            ::close(client_fd);
            return;
        }
        // For other errors, try to process what we have (if any)
        if (request_raw.empty()) {
            ::close(client_fd);
            return;
        }
    }

    // If we got here with n == -1 (timeout in Content-Length reading), skip processing
    if (n == -1) {
        return;
    }

    // Parse request line and headers
    HttpRequest req;
    req.method = "GET";
    req.path = "/";
    size_t header_end = request_raw.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        std::string head = request_raw.substr(0, header_end);
        std::istringstream hs(head);
        std::string request_line;
        std::getline(hs, request_line);
        if (!request_line.empty() && request_line.back() == '\r')
            request_line.pop_back();
        {
            std::istringstream rl(request_line);
            rl >> req.method >> req.path;  // ignore HTTP version
        }
        std::string line;
        while (std::getline(hs, line)) {
            if (line == "\r" || line.empty())
                break;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            auto colon = line.find(":");
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 1);
                while (!value.empty() && value.front() == ' ')
                    value.erase(value.begin());
                req.headers[key] = value;
            }
        }
        req.body = request_raw.substr(header_end + 4);
    }

    // Route
    HttpResponse resp;
    bool handled = false;
    if (req.path == "/health" && health_handler_) {
        resp = health_handler_(req);
        handled = true;
    } else if (req.path == "/orders" && order_handler_) {
        resp = order_handler_(req);
        handled = true;
    }
    if (!handled) {
        resp = createErrorResponse(404, "Not Found");
    }

    // Ensure Content-Type header
    if (resp.headers.find("Content-Type") == resp.headers.end()) {
        resp.headers["Content-Type"] = "application/json";
    }

    // Write response
    std::ostringstream out;
    out << "HTTP/1.1 " << resp.status_code << ' ' << reasonPhrase(resp.status_code) << "\r\n";
    out << "Content-Length: " << resp.body.size() << "\r\n";
    for (const auto& kv : resp.headers) {
        out << kv.first << ": " << kv.second << "\r\n";
    }
    out << "\r\n";
    out << resp.body;
    auto out_str = out.str();
    ::send(client_fd, out_str.data(), out_str.size(), 0);

    ::close(client_fd);
}

void HttpServer::stop() {
    if (!running_)
        return;
    stop_flag_ = true;
    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    running_ = false;
}

bool HttpServer::isRunning() const {
    return running_;
}

void HttpServer::setOrderHandler(RequestHandler handler) {
    order_handler_ = handler;
}

void HttpServer::setHealthHandler(RequestHandler handler) {
    health_handler_ = handler;
}

void HttpServer::setTimeout(int seconds) {
    timeout_seconds_ = seconds;
}

void HttpServer::setMaxConnections(int max_connections) {
    max_connections_ = max_connections;
}

void HttpServer::handleRequest(const HttpRequest& request) {
    (void)request;  // Unused in this minimal implementation
}

HttpResponse HttpServer::createErrorResponse(int status_code, const std::string& message) {
    HttpResponse response;
    response.status_code = status_code;
    response.body = "{\"error\": \"" + message + "\"}";
    response.headers["Content-Type"] = "application/json";
    return response;
}

}  // namespace network
}  // namespace trading