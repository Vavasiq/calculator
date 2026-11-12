#include "calculator/Calculator.h"
#include "calculator/Logger.h"

#include <mathlib/functions.h>

#include <string>

namespace calculator {

Task Calculator::calculate(Task task) const {
    Logger::instance().info(std::string("Calculating, op='") + task.operation + "'");

    int result = 0;
    int status = mathlib::MATH_DOMAIN;

    switch (task.operation) {
        case '+': status = mathlib::safe_add(task.firstValue, task.secondValue, result); break;
        case '-': status = mathlib::safe_sub(task.firstValue, task.secondValue, result); break;
        case '*': status = mathlib::safe_mul(task.firstValue, task.secondValue, result); break;
        case '/': status = mathlib::safe_div(task.firstValue, task.secondValue, result); break;
        case '^': status = mathlib::powi(task.firstValue, task.secondValue, result); break;
        case '!': status = mathlib::fact(task.firstValue, result); break;
        default: break;
    }

    task.status = status;
    task.result = status == mathlib::MATH_OK ? result : 0;
    return task;
}

} // namespace calculator
