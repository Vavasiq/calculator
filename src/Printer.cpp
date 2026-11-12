#include "calculator/Printer.h"
#include "calculator/Logger.h"

#include <mathlib/functions.h>

#include <iostream>
#include <string>

namespace calculator {

namespace {
std::string errorMessage(int status) {
    switch (status) {
        case mathlib::MATH_OVERFLOW: return "Integer overflow";
        case mathlib::MATH_DIV0: return "Division by zero";
        case mathlib::MATH_DOMAIN: return "Value out of domain";
        default: return "Unknown math error";
    }
}
} // namespace

void Printer::printResult(const Task& task) const {
    if (task.status == mathlib::MATH_OK) {
        Logger::instance().info("Result: " + std::to_string(task.result));
        std::cout << task.result << '\n';
        return;
    }

    const std::string message = errorMessage(task.status);
    Logger::instance().error(message);
    std::cerr << "Error: " << message << '\n';
}

} // namespace calculator
