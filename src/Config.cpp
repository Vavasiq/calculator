#include "calculator/Config.h"

#include <boost/asio/ip/address.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace calculator {

namespace {

// Ключ отсутствует -> target не меняется (остаётся значение по умолчанию).
void readString(const nlohmann::json& section, const char* key, const std::string& fullName, std::string& target) {
    if (!section.contains(key)) {
        return;
    }
    const auto& value = section.at(key);
    if (!value.is_string()) {
        throw std::invalid_argument("Config field '" + fullName + "' must be a string");
    }
    target = value.get<std::string>();
}

void readPort(const nlohmann::json& section, const char* key, const std::string& fullName, std::uint16_t& target) {
    if (!section.contains(key)) {
        return;
    }
    const auto& value = section.at(key);
    if (!value.is_number_integer()) {
        throw std::invalid_argument("Config field '" + fullName + "' must be an integer");
    }
    const auto port = value.get<std::int64_t>();
    if (port < 1 || port > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("Config field '" + fullName + "' must be in range 1..65535");
    }
    target = static_cast<std::uint16_t>(port);
}

} // namespace

AppConfig parseConfig(const std::string& json) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::invalid_argument(std::string("Config JSON parse error: ") + e.what());
    }

    if (!root.is_object()) {
        throw std::invalid_argument("Config root must be a JSON object");
    }

    AppConfig config;

    if (root.contains("database")) {
        const auto& database = root.at("database");
        if (!database.is_object()) {
            throw std::invalid_argument("Config field 'database' must be an object");
        }
        readString(database, "host", "database.host", config.database.host);
        readPort(database, "port", "database.port", config.database.port);
        readString(database, "name", "database.name", config.database.database);
        readString(database, "user", "database.user", config.database.username);
        readString(database, "password", "database.password", config.database.password);
    }

    if (root.contains("server")) {
        const auto& server = root.at("server");
        if (!server.is_object()) {
            throw std::invalid_argument("Config field 'server' must be an object");
        }
        readString(server, "address", "server.address", config.server.address);
        readPort(server, "port", "server.port", config.server.port);

        // Проверяем адрес здесь, а не при bind: ошибка конфига отсекается в main до запуска потоков.
        boost::system::error_code error;
        boost::asio::ip::make_address(config.server.address, error);
        if (error) {
            throw std::invalid_argument("Config field 'server.address' is not a valid IP address: '" +
                                        config.server.address + "'");
        }
    }

    return config;
}

AppConfig loadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open config file: " + path);
    }

    std::ostringstream content;
    content << file.rdbuf();

    try {
        return parseConfig(content.str());
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(path + ": " + e.what());
    }
}

} // namespace calculator
