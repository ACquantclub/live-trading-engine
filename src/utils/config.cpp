#include "trading/utils/config.hpp"
#include <fstream>
#include <sstream>

namespace trading::utils {

Expected<void, ConfigError> Config::loadFromFile(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        return ConfigError::FILE_NOT_FOUND;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadFromJson(buffer.str());
}

Expected<void, ConfigError> Config::loadFromJson(std::string_view json_string) {
    // TODO: Implement JSON parsing (would use nlohmann/json in real implementation)
    return parseJsonObject(json_string);
}

Expected<std::string, ConfigError> Config::getString(std::string_view key) const {
    auto result = getValue(key);
    if (!result) {
        return result.error();
    }
    return result.value();
}

Expected<int, ConfigError> Config::getInt(std::string_view key) const {
    auto result = getValue(key);
    if (!result) {
        return result.error();
    }

    try {
        return std::stoi(result.value());
    } catch (...) {
        return ConfigError::TYPE_MISMATCH;
    }
}

Expected<double, ConfigError> Config::getDouble(std::string_view key) const {
    auto result = getValue(key);
    if (!result) {
        return result.error();
    }

    try {
        return std::stod(result.value());
    } catch (...) {
        return ConfigError::TYPE_MISMATCH;
    }
}

Expected<bool, ConfigError> Config::getBool(std::string_view key) const {
    auto result = getValue(key);
    if (!result) {
        return result.error();
    }

    const std::string& value = result.value();
    return (value == "true" || value == "1" || value == "yes");
}

std::string Config::getString(std::string_view key, std::string_view default_value) const {
    auto it = config_data_.find(key);
    return (it != config_data_.end()) ? it->second : std::string(default_value);
}

int Config::getInt(std::string_view key, int default_value) const noexcept {
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        try {
            return std::stoi(it->second);
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

double Config::getDouble(std::string_view key, double default_value) const noexcept {
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        try {
            return std::stod(it->second);
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

bool Config::getBool(std::string_view key, bool default_value) const noexcept {
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        const std::string& value = it->second;
        return (value == "true" || value == "1" || value == "yes");
    }
    return default_value;
}

void Config::setString(std::string_view key, std::string_view value) {
    config_data_[std::string(key)] = std::string(value);
}

void Config::setInt(std::string_view key, int value) {
    config_data_[std::string(key)] = std::to_string(value);
}

void Config::setDouble(std::string_view key, double value) {
    config_data_[std::string(key)] = std::to_string(value);
}

void Config::setBool(std::string_view key, bool value) {
    config_data_[std::string(key)] = value ? "true" : "false";
}

bool Config::hasKey(std::string_view key) const {
    return config_data_.contains(key);  // C++20 feature, compatible with C++23
}

std::vector<std::string> Config::getKeys() const {
    std::vector<std::string> keys;
    keys.reserve(config_data_.size());
    for (const auto& [key, value] : config_data_) {  // Structured bindings (C++17)
        keys.push_back(key);
    }
    return keys;
}

void Config::clear() noexcept {
    config_data_.clear();
    sections_.clear();
}

Expected<std::shared_ptr<Config>, ConfigError> Config::getSection(
    std::string_view section_name) const {
    auto it = sections_.find(section_name);
    if (it != sections_.end()) {
        return it->second;
    }
    return ConfigError::KEY_NOT_FOUND;
}

Expected<std::string, ConfigError> Config::getValue(std::string_view key) const {
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        return it->second;
    }
    return ConfigError::KEY_NOT_FOUND;
}

Expected<void, ConfigError> Config::parseJsonObject(std::string_view json_string) {
    // TODO: Implement JSON object parsing with proper error handling
    (void)json_string;  // Suppress unused parameter warning
    return {};          // Success for now
}

}  // namespace trading::utils