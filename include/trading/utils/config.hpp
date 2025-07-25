#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace trading::utils {

enum class ConfigError : std::uint8_t {
    FILE_NOT_FOUND,
    INVALID_JSON,
    KEY_NOT_FOUND,
    TYPE_MISMATCH
};

// Simple expected-like implementation using std::variant for compatibility
template <typename T, typename E>
class Expected {
  private:
    std::variant<T, E> data_;

  public:
    Expected(const T& value) : data_(value) {
    }
    Expected(T&& value) : data_(std::move(value)) {
    }
    Expected(const E& error) : data_(error) {
    }
    Expected(E&& error) : data_(std::move(error)) {
    }

    bool has_value() const noexcept {
        return std::holds_alternative<T>(data_);
    }
    explicit operator bool() const noexcept {
        return has_value();
    }

    const T& value() const& {
        return std::get<T>(data_);
    }
    T& value() & {
        return std::get<T>(data_);
    }
    T&& value() && {
        return std::get<T>(std::move(data_));
    }

    const T& operator*() const& {
        return std::get<T>(data_);
    }
    T& operator*() & {
        return std::get<T>(data_);
    }
    T&& operator*() && {
        return std::get<T>(std::move(data_));
    }

    const E& error() const& {
        return std::get<E>(data_);
    }
    E& error() & {
        return std::get<E>(data_);
    }
    E&& error() && {
        return std::get<E>(std::move(data_));
    }
};

// Specialization for void success type
template <typename E>
class Expected<void, E> {
  private:
    std::variant<std::monostate, E> data_;

  public:
    Expected() : data_(std::monostate{}) {
    }
    Expected(const E& error) : data_(error) {
    }
    Expected(E&& error) : data_(std::move(error)) {
    }

    bool has_value() const noexcept {
        return std::holds_alternative<std::monostate>(data_);
    }
    explicit operator bool() const noexcept {
        return has_value();
    }

    const E& error() const& {
        return std::get<E>(data_);
    }
    E& error() & {
        return std::get<E>(data_);
    }
    E&& error() && {
        return std::get<E>(std::move(data_));
    }
};

class Config {
  public:
    Config() = default;
    ~Config() = default;

    // Copy and move operations
    Config(const Config&) = default;
    Config& operator=(const Config&) = default;
    Config(Config&&) = default;
    Config& operator=(Config&&) = default;

    // Loading configuration - returns Expected for better error handling
    [[nodiscard]] Expected<void, ConfigError> loadFromFile(const std::string& config_file);
    [[nodiscard]] Expected<void, ConfigError> loadFromJson(std::string_view json_string);

    // Getters - using our Expected implementation
    [[nodiscard]] Expected<std::string, ConfigError> getString(std::string_view key) const;
    [[nodiscard]] Expected<int, ConfigError> getInt(std::string_view key) const;
    [[nodiscard]] Expected<double, ConfigError> getDouble(std::string_view key) const;
    [[nodiscard]] Expected<bool, ConfigError> getBool(std::string_view key) const;

    // Getters with default values (C++17 compatible fallback)
    [[nodiscard]] std::string getString(std::string_view key, std::string_view default_value) const;
    [[nodiscard]] int getInt(std::string_view key, int default_value) const noexcept;
    [[nodiscard]] double getDouble(std::string_view key, double default_value) const noexcept;
    [[nodiscard]] bool getBool(std::string_view key, bool default_value) const noexcept;

    // Setters
    void setString(std::string_view key, std::string_view value);
    void setInt(std::string_view key, int value);
    void setDouble(std::string_view key, double value);
    void setBool(std::string_view key, bool value);

    // Utility methods
    [[nodiscard]] bool hasKey(std::string_view key) const;
    [[nodiscard]] std::vector<std::string> getKeys() const;
    void clear() noexcept;

    // Nested configuration
    [[nodiscard]] Expected<std::shared_ptr<Config>, ConfigError> getSection(
        std::string_view section_name) const;

  private:
    std::map<std::string, std::string, std::less<>> config_data_;
    std::map<std::string, std::shared_ptr<Config>, std::less<>> sections_;

    [[nodiscard]] Expected<std::string, ConfigError> getValue(std::string_view key) const;
    [[nodiscard]] Expected<void, ConfigError> parseJsonObject(std::string_view json_string);
};

}  // namespace trading::utils