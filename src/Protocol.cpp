#include "calculator/Protocol.h"

#include <mathlib/functions.h>
#include <nlohmann/json.hpp>

namespace calculator {

std::string statusMessage(int status) {
    switch (status) {
        case mathlib::MATH_OK: return "OK";
        case mathlib::MATH_OVERFLOW: return "Integer overflow";
        case mathlib::MATH_DIV0: return "Division by zero";
        case mathlib::MATH_DOMAIN: return "Value out of domain";
        case kStatusInvalidRequest: return "Invalid request";
        case kStatusInternalError: return "Internal error";
        default: return "Unknown error";
    }
}

std::string serializeResponse(const Response& response) {
    nlohmann::json json;
    json["status"] = response.status;
    json["result"] = response.result;
    if (response.status != mathlib::MATH_OK) {
        json["error"] = response.error.empty() ? statusMessage(response.status) : response.error;
    }

    // Текст ошибки разбора от nlohmann содержит фрагмент присланного клиентом ввода,
    // а там может быть невалидный UTF-8. Обычный dump() в этом случае бросает type_error,
    // поэтому такие байты заменяются на U+FFFD.
    return json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace calculator
