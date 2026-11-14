#pragma once

#include <cstddef>
#include <string>

namespace calculator {

// Протокол (план: docs/task4-3-network.md, раздел 4).
// Запрос — одна строка JSON с '\n', ответ — одна строка JSON с '\n':
//   {"result":5,"status":0}
//   {"error":"Division by zero","result":0,"status":2}
//
// status: 0..3 — коды mathlib (MATH_OK, MATH_OVERFLOW, MATH_DIV0, MATH_DOMAIN), те же, что в БД;
// отрицательные — коды протокола, в БД не попадают.
inline constexpr int kStatusInvalidRequest = -1;
inline constexpr int kStatusInternalError = -2;

// Максимальная длина строки запроса вместе с '\n'.
inline constexpr std::size_t kMaxRequestSize = 4096;

struct Response {
    int status{0};
    int result{0};
    std::string error; // только при status != 0
};

// Текст для кода статуса: ошибки mathlib и коды протокола.
std::string statusMessage(int status);

// JSON-строка ответа без '\n' (перевод строки добавляет сервер). Поле error пишется только
// при status != 0; если оно пустое, подставляется statusMessage(status).
// Не бросает исключений из-за невалидного UTF-8 в error: такие байты заменяются на U+FFFD.
std::string serializeResponse(const Response& response);

} // namespace calculator
