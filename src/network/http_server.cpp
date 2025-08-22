#include "trading/network/http_server.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <regex>
#include <sstream>

namespace trading {
namespace network {

namespace {
std::string reasonPhrase(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 201:
            return "Created";
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

// Helper function to URL decode a string
std::string urlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            std::string hex = str.substr(i + 1, 2);
            char* end;
            long value = std::strtol(hex.c_str(), &end, 16);
            if (*end == 0) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

// Helper function to parse query parameters from a query string
std::map<std::string, std::string> parseQueryParameters(const std::string& query_string) {
    std::map<std::string, std::string> params;
    if (query_string.empty()) {
        return params;
    }

    std::istringstream qs(query_string);
    std::string pair;
    while (std::getline(qs, pair, '&')) {
        auto equals_pos = pair.find('=');
        if (equals_pos != std::string::npos) {
            std::string key = urlDecode(pair.substr(0, equals_pos));
            std::string value = urlDecode(pair.substr(equals_pos + 1));
            params[key] = value;
        } else {
            // Parameter without value
            params[urlDecode(pair)] = "";
        }
    }
    return params;
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
            std::string full_path;
            rl >> req.method >> full_path;  // ignore HTTP version

            // Separate path from query string
            size_t query_pos = full_path.find('?');
            if (query_pos != std::string::npos) {
                req.path = full_path.substr(0, query_pos);
                std::string query_string = full_path.substr(query_pos + 1);
                req.query_params = parseQueryParameters(query_string);
            } else {
                req.path = full_path;
            }
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
    HttpResponse resp = routeRequest(req);

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

void HttpServer::registerRoute(const std::string& method, const std::string& path_pattern,
                               RequestHandler handler) {
    Route route;
    route.method = method;
    route.path_pattern = path_pattern;
    route.path_regex = pathPatternToRegex(path_pattern, route.param_names);
    route.handler = handler;
    routes_.push_back(route);
}

std::regex HttpServer::pathPatternToRegex(const std::string& pattern,
                                          std::vector<std::string>& param_names) {
    param_names.clear();
    std::string regex_pattern = pattern;

    // Replace {param} with named capture groups
    std::regex param_regex(R"(\{([^}]+)\})");
    std::smatch match;
    std::string::const_iterator start = regex_pattern.cbegin();
    std::string result;

    while (std::regex_search(start, regex_pattern.cend(), match, param_regex)) {
        result += std::string(start, match[0].first);
        param_names.push_back(match[1].str());
        result += "([^/]+)";  // Capture group for the parameter
        start = match[0].second;
    }
    result += std::string(start, regex_pattern.cend());

    // Escape other regex special characters and ensure exact match
    return std::regex("^" + result + "$");
}

HttpResponse HttpServer::routeRequest(const HttpRequest& request) {
    // Fast path for high-frequency endpoints - avoid regex overhead
    if (request.method == "POST" && request.path == "/order" && !routes_.empty()) {
        // Find the /order route directly
        for (const auto& route : routes_) {
            if (route.method == "POST" && route.path_pattern == "/order") {
                return route.handler(request);
            }
        }
    }

    if (request.method == "GET" && request.path == "/health" && !routes_.empty()) {
        // Find the /health route directly
        for (const auto& route : routes_) {
            if (route.method == "GET" && route.path_pattern == "/health") {
                return route.handler(request);
            }
        }
    }

    // For parameterized routes, use regex matching
    for (const auto& route : routes_) {
        if (route.method == request.method || route.method == "*") {
            // Skip simple routes we already handled above
            if ((route.path_pattern == "/order" && route.method == "POST") ||
                (route.path_pattern == "/health" && route.method == "GET")) {
                continue;
            }

            std::smatch match;
            if (std::regex_match(request.path, match, route.path_regex)) {
                // Extract path parameters only for parameterized routes
                if (!route.param_names.empty()) {
                    HttpRequest modified_request = request;
                    for (size_t i = 0; i < route.param_names.size() && i + 1 < match.size(); ++i) {
                        modified_request.path_params[route.param_names[i]] = match[i + 1].str();
                    }
                    return route.handler(modified_request);
                } else {
                    return route.handler(request);
                }
            }
        }
    }

    // Fall back to legacy handlers for backward compatibility
    if (request.path == "/health" && health_handler_) {
        return health_handler_(request);
    } else if (request.path == "/orders" && order_handler_) {
        return order_handler_(request);
    }

    return createErrorResponse(404, "Not Found");
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