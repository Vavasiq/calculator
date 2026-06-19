#include "calculator/Parser.h"
#include "calculator/Logger.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

CalcData Parser::parse(const std::string& json_str) const {
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
    std::string op_str = j["op"].get<std::string>();
    if (op_str.size() != 1) {
        throw std::invalid_argument("Field 'op' must be a single character");
    }

    if (!j.contains("a") || !j["a"].is_number_integer()) {
        throw std::invalid_argument("Missing or invalid field: 'a'");
    }

    CalcData data;
    data.op  = op_str[0];
    data.a   = j["a"].get<int>();

    if (data.op != '!') {
        if (!j.contains("b") || !j["b"].is_number_integer()) {
            throw std::invalid_argument("Missing or invalid field: 'b'");
    }

        data.b = j["b"].get<int>();
    }

    return data;
}
