#include "calculator/Checker.h"
#include "calculator/Logger.h"

#include <stdexcept>
#include <string>

namespace calculator {

void Checker::check(const Task& task) const {
    Logger::instance().info(std::string("Checking op='") + task.operation + "'");

    const std::string valid_ops = "+-*/^!";
    if (valid_ops.find(task.operation) == std::string::npos) {
        throw std::invalid_argument(std::string("Unknown operator: '") + task.operation + "'");
    }
}

} // namespace calculator
