#include "calculator/Parser.h"
#include "calculator/Logger.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace calculator {

namespace {

// Целое поле запроса в диапазоне int. is_number_integer() пропускает и 3000000000,
// а get<int>() молча превратил бы его в другое число — теперь это ошибка запроса.
// Числа за пределами int64/uint64 nlohmann разбирает как дробные — их отсекает is_number_integer().
int readInt(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || !j.at(key).is_number_integer()) {
        throw std::invalid_argument(std::string("Missing or invalid field: '") + key + "'");
    }

    const auto& value = j.at(key);
    constexpr auto kMin = std::numeric_limits<int>::min();
    constexpr auto kMax = std::numeric_limits<int>::max();
    const bool inRange = value.is_number_unsigned()
                             ? value.get<std::uint64_t>() <= static_cast<std::uint64_t>(kMax)
                             : value.get<std::int64_t>() >= kMin && value.get<std::int64_t>() <= kMax;
    if (!inRange) {
        throw std::invalid_argument(std::string("Field '") + key + "' is out of int range");
    }
    return value.get<int>();
}

} // namespace

Task Parser::parse(const std::string& json_str) const {
    Logger::instance().info("Parsing input: " + json_str);

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_str);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::invalid_argument(std::string("JSON parse error: ") + e.what());
    }

    if (!j.contains("op") || !j["op"].is_string()) {
        throw std::invalid_argument("Missing or invalid field: 'op'");
    }
    const std::string op_str = j["op"].get<std::string>();
    if (op_str.size() != 1) {
        throw std::invalid_argument("Field 'op' must be a single character");
    }

    Task task;
    task.operation = op_str[0];
    task.firstValue = readInt(j, "a");

    if (task.operation != '!') {
        task.secondValue = readInt(j, "b");
    }

    return task;
}

} // namespace calculator
