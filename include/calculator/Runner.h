#pragma once

#include "calculator/Calculator.h"
#include "calculator/Checker.h"
#include "calculator/Parser.h"
#include "calculator/Protocol.h"
#include "calculator/TaskStorage.h"

#include <string>

namespace calculator {

// Обработка одного запроса: разбор -> проверка -> кеш -> расчёт -> запись в хранилище.
// Ничего не печатает, результат возвращается как Response.
class Runner {
public:
    // Хранилищем Runner не владеет: оно должно пережить Runner.
    explicit Runner(TaskStorage& storage);

    // Не бросает: ошибки запроса -> kStatusInvalidRequest с текстом ошибки,
    // прочие (например, сбой БД) -> kStatusInternalError, подробности только в лог.
    Response handle(const std::string& requestLine);

private:
    TaskStorage& storage_;
    Parser parser_;
    Checker checker_;
    Calculator calculator_;
};

} // namespace calculator
