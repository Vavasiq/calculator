#include <gtest/gtest.h>

#include "calculator/Calculator.h"
#include "calculator/Checker.h"
#include "calculator/Parser.h"
#include "calculator/Task.h"

#include <mathlib/functions.h>

#include <stdexcept>

using namespace calculator;

// Parser

TEST(ParserTest, ParsesAddition) {
    Parser p;
    const auto task = p.parse(R"({"op":"+","a":3,"b":4})");
    EXPECT_EQ(task.operation, '+');
    EXPECT_EQ(task.firstValue, 3);
    EXPECT_EQ(task.secondValue, 4);
}

TEST(ParserTest, ParsesFactorial) {
    Parser p;
    const auto task = p.parse(R"({"op":"!","a":5})");
    EXPECT_EQ(task.operation, '!');
    EXPECT_EQ(task.firstValue, 5);
}

TEST(ParserTest, ThrowsOnInvalidJson) {
    Parser p;
    EXPECT_THROW(p.parse("not_json"), std::invalid_argument);
}

TEST(ParserTest, ThrowsOnMissingOp) {
    Parser p;
    EXPECT_THROW(p.parse(R"({"a":1,"b":2})"), std::invalid_argument);
}

TEST(ParserTest, ThrowsOnMissingA) {
    Parser p;
    EXPECT_THROW(p.parse(R"({"op":"+","b":2})"), std::invalid_argument);
}

TEST(ParserTest, ThrowsOnMissingBForBinaryOp) {
    Parser p;
    EXPECT_THROW(p.parse(R"({"op":"+","a":1})"), std::invalid_argument);
}

TEST(ParserTest, ThrowsOnMulticharOp) {
    Parser p;
    EXPECT_THROW(p.parse(R"({"op":"++","a":1,"b":2})"), std::invalid_argument);
}

// Checker

TEST(CheckerTest, AcceptsValidAddition) {
    Checker c;
    EXPECT_NO_THROW(c.check({1, 2, '+'}));
}

TEST(CheckerTest, ThrowsOnUnknownOp) {
    Checker c;
    EXPECT_THROW(c.check({1, 2, '%'}), std::invalid_argument);
}

// Calculator

TEST(CalculatorTest, Add) {
    Calculator calc;
    const auto task = calc.calculate({150, 150, '+'});
    EXPECT_EQ(task.result, 300);
    EXPECT_EQ(task.status, mathlib::MATH_OK);
}

TEST(CalculatorTest, Sub) {
    Calculator calc;
    const auto task = calc.calculate({1337, 420, '-'});
    EXPECT_EQ(task.result, 917);
    EXPECT_EQ(task.status, mathlib::MATH_OK);
}

TEST(CalculatorTest, Mul) {
    Calculator calc;
    const auto task = calc.calculate({6, 7, '*'});
    EXPECT_EQ(task.result, 42);
    EXPECT_EQ(task.status, mathlib::MATH_OK);
}

TEST(CalculatorTest, Div) {
    Calculator calc;
    const auto task = calc.calculate({42069, 3, '/'});
    EXPECT_EQ(task.result, 14023);
    EXPECT_EQ(task.status, mathlib::MATH_OK);
}

TEST(CalculatorTest, DivisionByZeroHasStatus) {
    Calculator calc;
    const auto task = calc.calculate({5, 0, '/'});
    EXPECT_EQ(task.result, 0);
    EXPECT_EQ(task.status, mathlib::MATH_DIV0);
}

TEST(CalculatorTest, OverflowHasStatus) {
    Calculator calc;
    const auto task = calc.calculate({2147483647, 1, '+'});
    EXPECT_EQ(task.result, 0);
    EXPECT_EQ(task.status, mathlib::MATH_OVERFLOW);
}

TEST(CalculatorTest, NegativeExponentHasDomainStatus) {
    Calculator calc;
    const auto task = calc.calculate({2, -3, '^'});
    EXPECT_EQ(task.status, mathlib::MATH_DOMAIN);
}

TEST(CalculatorTest, NegativeFactorialHasDomainStatus) {
    Calculator calc;
    const auto task = calc.calculate({-1, 0, '!'});
    EXPECT_EQ(task.status, mathlib::MATH_DOMAIN);
}

// Cache key

TEST(TaskKeyTest, AdditionIsCommutative) {
    EXPECT_EQ(makeTaskKey({1, 2, '+'}), makeTaskKey({2, 1, '+'}));
}

TEST(TaskKeyTest, MultiplicationIsCommutative) {
    EXPECT_EQ(makeTaskKey({3, 7, '*'}), makeTaskKey({7, 3, '*'}));
}

TEST(TaskKeyTest, SubtractionIsNotCommutative) {
    EXPECT_NE(makeTaskKey({1, 2, '-'}), makeTaskKey({2, 1, '-'}));
}
