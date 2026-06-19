#include "calculator/Calculator.h"
#include "calculator/Logger.h"
#include <mathlib/functions.h>

#include <stdexcept>
#include <string>

int Calculator::checked(int code, int result) const {
    switch (code) {
        case mathlib::MATH_OK:       return result;
        case mathlib::MATH_OVERFLOW: throw std::overflow_error("Integer overflow");
        case mathlib::MATH_DIV0:     throw std::domain_error("Division by zero");
        case mathlib::MATH_DOMAIN:   throw std::domain_error("Value out of domain");
        default:                     throw std::runtime_error("Unknown math error");
    }
}

int Calculator::calculate(const CalcData& data) const {
    Logger::instance().info(std::string("Calculating, op='") + data.op + "'");

    int result = 0;
    int code   = mathlib::MATH_DOMAIN;

    switch (data.op) {
        case '+': code = mathlib::safe_add(data.a, data.b, result); break;
        case '-': code = mathlib::safe_sub(data.a, data.b, result); break;
        case '*': code = mathlib::safe_mul(data.a, data.b, result); break;
        case '/': code = mathlib::safe_div(data.a, data.b, result); break;
        case '^': code = mathlib::powi(data.a, data.b, result);     break;
        case '!': code = mathlib::fact(data.a, result);             break;
        default:  throw std::invalid_argument(std::string("Unknown operator: '") + data.op + "'");
    }

    return checked(code, result);
}
