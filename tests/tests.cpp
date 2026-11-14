#include <gtest/gtest.h>

#include "calculator/Calculator.h"
#include "calculator/Checker.h"
#include "calculator/Parser.h"
#include "calculator/StopFlag.h"
#include "calculator/Task.h"

#include <mathlib/functions.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <stdexcept>
#include <thread>

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

// StopFlag

TEST(StopFlagTest, InitiallyNotRequested) {
    StopFlag flag;
    EXPECT_FALSE(flag.isStopRequested());
    EXPECT_EQ(flag.reason(), StopReason::None);
    EXPECT_FALSE(flag.waitForStopFor(std::chrono::milliseconds(0)));
}

TEST(StopFlagTest, RequestBeforeWaitIsNotLost) {
    StopFlag flag;
    flag.requestStop(StopReason::Signal, SIGTERM);

    // Нулевой таймаут: проходит только если предикат уже истинен, без ожидания уведомления.
    EXPECT_TRUE(flag.waitForStopFor(std::chrono::milliseconds(0)));
    EXPECT_TRUE(flag.isStopRequested());
    EXPECT_EQ(flag.reason(), StopReason::Signal);
    EXPECT_EQ(flag.signalNumber(), SIGTERM);
}

TEST(StopFlagTest, WakesWaiterFromOtherThread) {
    using std::chrono::seconds;
    using std::chrono::steady_clock;

    StopFlag flag;
    std::atomic<bool> woken{false};
    std::thread waiter([&] { woken = flag.waitForStopFor(seconds(2)); });

    // Busy-цикл с таймаутом вместо sleep: ждём, пока поток реально войдёт в cv.wait,
    // иначе requestStop мог бы успеть раньше и тест проверял бы не пробуждение.
    const auto deadline = steady_clock::now() + seconds(2);
    while (flag.waitingThreads() == 0 && steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool waiterStarted = flag.waitingThreads() == 1;

    // Останавливаем и join'им в любом случае: joinable std::thread в деструкторе -> terminate.
    flag.requestStop(StopReason::Signal, SIGTERM);
    waiter.join();

    ASSERT_TRUE(waiterStarted) << "waiter thread never entered waitForStopFor";
    EXPECT_TRUE(woken);
    EXPECT_EQ(flag.waitingThreads(), 0u);
}

TEST(StopFlagTest, FirstReasonWins) {
    StopFlag flag;
    flag.requestStop(StopReason::Signal, SIGTERM);
    flag.requestStop(StopReason::Error);

    EXPECT_EQ(flag.reason(), StopReason::Signal);
    EXPECT_EQ(flag.signalNumber(), SIGTERM);
}
