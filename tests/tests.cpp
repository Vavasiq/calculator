#include <gtest/gtest.h>

#include "calculator/Calculator.h"
#include "calculator/Checker.h"
#include "calculator/Parser.h"
#include "calculator/Runner.h"

#include <stdexcept>
// Parser

TEST(ParserTest, ParsesAddition) {
    Parser p;
    auto d = p.parse(R"({"op":"+","a":3,"b":4})");
    EXPECT_EQ(d.op, '+');
    EXPECT_EQ(d.a, 3);
    EXPECT_EQ(d.b, 4);
}

TEST(ParserTest, ParsesFactorial) {
    Parser p;
    auto d = p.parse(R"({"op":"!","a":5})");
    EXPECT_EQ(d.op, '!');
    EXPECT_EQ(d.a, 5);
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
    CalcData d{1, 2, '+'};
    EXPECT_NO_THROW(c.check(d));
}

TEST(CheckerTest, ThrowsOnUnknownOp) {
    Checker c;
    CalcData d{1, 2, '%'};
    EXPECT_THROW(c.check(d), std::invalid_argument);
}

TEST(CheckerTest, ThrowsOnDivisionByZero) {
    Checker c;
    CalcData d{5, 0, '/'};
    EXPECT_THROW(c.check(d), std::domain_error);
}

TEST(CheckerTest, ThrowsOnNegativeFactorial) {
    Checker c;
    CalcData d{-1, 0, '!'};
    EXPECT_THROW(c.check(d), std::domain_error);
}

TEST(CheckerTest, ThrowsOnNegativeExponent) {
    Checker c;
    CalcData d{2, -3, '^'};
    EXPECT_THROW(c.check(d), std::domain_error);
}

// Calculator

TEST(CalculatorTest, Add) {
    Calculator calc;
    EXPECT_EQ(calc.calculate({150, 150, '+'}), 300);
}

TEST(CalculatorTest, Sub) {
    Calculator calc;
    EXPECT_EQ(calc.calculate({1337, 420, '-'}), 917);
}

TEST(CalculatorTest, Mul) {
    Calculator calc;
    EXPECT_EQ(calc.calculate({6, 7, '*'}), 42);
}

TEST(CalculatorTest, Div) {
    Calculator calc;
    EXPECT_EQ(calc.calculate({42069, 3, '/'}), 14023);
}

TEST(CalculatorTest, Pow) {
    Calculator calc;
    EXPECT_EQ(calc.calculate({2, 10, '^'}), 1024);
}

TEST(CalculatorTest, PowZeroExponent) {
    Calculator calc;
    EXPECT_EQ(calc.calculate({1991, 0, '^'}), 1);
}

TEST(CalculatorTest, Factorial) {
    Calculator calc;
    EXPECT_EQ(calc.calculate({5, 0, '!'}), 120);
}

TEST(CalculatorTest, FactorialZero) {
    Calculator calc;
    EXPECT_EQ(calc.calculate({0, 0, '!'}), 1);
}

TEST(CalculatorTest, ThrowsOnOverflow) {
    Calculator calc;
    EXPECT_THROW(calc.calculate({2147483647, 1, '+'}), std::overflow_error);
}

// Runner

TEST(RunnerTest, ReturnsZeroOnSuccess) {
    Runner r;
    EXPECT_EQ(r.run(R"({"op":"+","a":1,"b":2})"), 0);
}

TEST(RunnerTest, ReturnsOneOnBadJson) {
    Runner r;
    EXPECT_EQ(r.run("garbage"), 1);
}

TEST(RunnerTest, ReturnsOneOnDivisionByZero) {
    Runner r;
    EXPECT_EQ(r.run(R"({"op":"/","a":5,"b":0})"), 1);
}

TEST(RunnerTest, ReturnsOneOnOverflow) {
    Runner r;
    EXPECT_EQ(r.run(R"({"op":"*","a":2147483647,"b":2})"), 1);
}
