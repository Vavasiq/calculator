#include "calculator/Runner.h"
#include "calculator/Logger.h"

#include <mathlib/functions.h>

#include <exception>
#include <stdexcept>
#include <string>

namespace calculator {

Runner::Runner(TaskStorage& storage): storage_(storage) {}

Response Runner::handle(const std::string& requestLine) {
    try {
        Task task = parser_.parse(requestLine);
        checker_.check(task);

        if (const auto cached = storage_.getRecord(task)) {
            task = *cached;
        } else {
            task = calculator_.calculate(task);
            storage_.writeRecord(task);
        }

        if (task.status == mathlib::MATH_OK) {
            Logger::instance().info("Result: " + std::to_string(task.result));
            return Response{task.status, task.result, {}};
        }

        // Ошибка вычисления — нормальный ответ клиенту, а не сбой сервиса.
        const std::string message = statusMessage(task.status);
        Logger::instance().info("Math error: " + message);
        return Response{task.status, 0, message};
    } catch (const std::invalid_argument& e) {
        // Parser и Checker сообщают о некорректном запросе через invalid_argument.
        Logger::instance().warn(std::string("Invalid request: ") + e.what());
        return Response{kStatusInvalidRequest, 0, e.what()};
    } catch (const std::exception& e) {
        // Например, сбой записи в БД. Детали — только в лог, клиенту без подробностей.
        Logger::instance().error(std::string("Internal error: ") + e.what());
        return Response{kStatusInternalError, 0, statusMessage(kStatusInternalError)};
    }
}

} // namespace calculator
