#include "calculator/Checker.h"
#include "calculator/Logger.h"

#include <stdexcept>
#include <string>

void Checker::check(const CalcData& data) const {
    Logger::instance().info(std::string("Checking op='") + data.op + "'");

    const std::string valid_ops = "+-*/^!";

    if (valid_ops.find(data.op) == std::string::npos) {
        throw std::invalid_argument(std::string("Unknown operator: '") + data.op + "'");
    }

    if (data.op == '/' && data.b == 0) {
        throw std::domain_error("Division by zero");
    }

    if (data.op == '!' && data.a < 0) {
        throw std::domain_error("Factorial of negative number");
    }

    if (data.op == '^' && data.b < 0) {
        throw std::domain_error("Negative exponent is not supported");
    }
}
