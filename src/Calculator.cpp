#include "calculator/Calculator.h"
#include "calculator/Logger.h"

#include <mathlib/functions.h>

#include <string>

namespace calculator {

namespace {

// Защита от тяжёлых входов из сети (план: docs/task4-3-network.md, раздел 13).
// Результаты совпадают с mathlib, меняется только цена вычисления.

// 13! = 6227020800 уже не помещается в int.
constexpr int kMaxFactorialArgument = 12;

// mathlib::fact рекурсивна на глубину n: {"op":"!","a":100000000} переполнил бы стек
// рабочего потока (SIGSEGV). При n > 12 ответ заведомо MATH_OVERFLOW — без рекурсии.
int factorial(int n, int& out) {
    if (n > kMaxFactorialArgument) {
        return mathlib::MATH_OVERFLOW;
    }
    return mathlib::fact(n, out); // n < 0 -> MATH_DOMAIN, как и раньше
}

// mathlib::powi крутит цикл exp раз: для оснований 0, 1, -1 переполнения не бывает,
// и exp = 2000000000 занял бы рабочий поток на секунды. Эти основания считаем сразу.
// При |base| >= 2 переполнение наступает не позже чем за 32 итерации.
int power(int base, int exp, int& out) {
    if (exp >= 0) {
        switch (base) {
            case 1: out = 1; return mathlib::MATH_OK;
            case 0: out = exp == 0 ? 1 : 0; return mathlib::MATH_OK;
            case -1: out = exp % 2 == 0 ? 1 : -1; return mathlib::MATH_OK;
            default: break;
        }
    }
    return mathlib::powi(base, exp, out); // exp < 0 -> MATH_DOMAIN, как и раньше
}

} // namespace

Task Calculator::calculate(Task task) const {
    Logger::instance().info(std::string("Calculating, op='") + task.operation + "'");

    int result = 0;
    int status = mathlib::MATH_DOMAIN;

    switch (task.operation) {
        case '+': status = mathlib::safe_add(task.firstValue, task.secondValue, result); break;
        case '-': status = mathlib::safe_sub(task.firstValue, task.secondValue, result); break;
        case '*': status = mathlib::safe_mul(task.firstValue, task.secondValue, result); break;
        case '/': status = mathlib::safe_div(task.firstValue, task.secondValue, result); break;
        case '^': status = power(task.firstValue, task.secondValue, result); break;
        case '!': status = factorial(task.firstValue, result); break;
        default: break;
    }

    task.status = status;
    task.result = status == mathlib::MATH_OK ? result : 0;
    return task;
}

} // namespace calculator
