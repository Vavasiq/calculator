#include <gtest/gtest.h>

#include "calculator/Calculator.h"
#include "calculator/Checker.h"
#include "calculator/Config.h"
#include "calculator/Parser.h"
#include "calculator/StopFlag.h"
#include "calculator/Task.h"

#include <mathlib/functions.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace calculator;

namespace {

// Busy-цикл с ограничением по времени вместо sleep: true, если условие выполнилось до дедлайна.
template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

} // namespace

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

// StopFlag: подписка на остановку

TEST(StopFlagTest, OnStopCalledOnRequest) {
    StopFlag flag;
    int calls = 0;
    auto subscription = flag.onStop([&] { ++calls; });

    EXPECT_EQ(calls, 0);
    flag.requestStop(StopReason::Signal, SIGTERM);
    EXPECT_EQ(calls, 1);
}

TEST(StopFlagTest, OnStopAfterRequestCalledImmediately) {
    StopFlag flag;
    flag.requestStop(StopReason::Signal, SIGTERM);

    bool called = false;
    auto subscription = flag.onStop([&] { called = true; });
    EXPECT_TRUE(called); // вызван внутри onStop, а не сохранён «на потом»
}

TEST(StopFlagTest, CallbackCalledExactlyOnce) {
    StopFlag flag;
    std::atomic<int> calls{0};
    auto subscription = flag.onStop([&] { ++calls; });

    // Два потока одновременно запрашивают остановку (как поток сигналов и рабочий поток
    // при ошибке): колбэк всё равно вызывается один раз.
    std::thread first([&] { flag.requestStop(StopReason::Signal, SIGTERM); });
    std::thread second([&] { flag.requestStop(StopReason::Error); });
    first.join();
    second.join();
    flag.requestStop(StopReason::Error);

    EXPECT_EQ(calls.load(), 1);
}

TEST(StopFlagTest, CallbackRunsOutsideLock) {
    // Колбэк обращается к StopFlag. Если бы он вызывался под мьютексом, это была бы
    // повторная блокировка std::mutex: зависание (его оборвёт TIMEOUT ctest) или отчёт TSan.
    StopFlag flag;
    bool seenFromRequest = false;
    auto subscription = flag.onStop([&] { seenFromRequest = flag.isStopRequested(); });
    flag.requestStop(StopReason::Signal, SIGTERM);
    EXPECT_TRUE(seenFromRequest);

    // То же для немедленного вызова из onStop.
    StopReason seenFromSubscribe = StopReason::None;
    auto late = flag.onStop([&] { seenFromSubscribe = flag.reason(); });
    EXPECT_EQ(seenFromSubscribe, StopReason::Signal);
}

TEST(StopFlagTest, UnsubscribedCallbackNotCalled) {
    StopFlag flag;
    bool called = false;
    {
        auto subscription = flag.onStop([&] { called = true; });
    } // отписка в деструкторе
    flag.requestStop(StopReason::Signal, SIGTERM);
    EXPECT_FALSE(called);
}

TEST(StopFlagTest, MovedSubscriptionKeepsCallback) {
    StopFlag flag;
    bool called = false;
    std::optional<StopFlag::Subscription> holder;
    {
        auto subscription = flag.onStop([&] { called = true; });
        holder.emplace(std::move(subscription));
    } // перемещённый объект уничтожен — подписка должна остаться у holder
    flag.requestStop(StopReason::Signal, SIGTERM);
    EXPECT_TRUE(called);
}

TEST(StopFlagTest, CallbackCanUnsubscribeItself) {
    // Колбэк уничтожает собственную подписку. Деструктор в том же потоке не должен ждать
    // сам себя — иначе зависание (его оборвёт TIMEOUT ctest).
    StopFlag flag;
    bool called = false;
    std::optional<StopFlag::Subscription> subscription;
    subscription.emplace(flag.onStop([&] {
        called = true;
        subscription.reset();
    }));
    flag.requestStop(StopReason::Signal, SIGTERM);
    EXPECT_TRUE(called);
    EXPECT_FALSE(subscription.has_value());
}

TEST(StopFlagTest, SubscriptionDestructorWaitsForRunningCallback) {
    // Сценарий из раздела 5 плана: поток сигналов выполняет колбэк, а рабочий поток в это
    // время уничтожает подписку (и затем io_context). Деструктор обязан дождаться колбэка.
    using std::chrono::milliseconds;

    StopFlag flag;
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<bool> callbackFinished{false};
    std::atomic<bool> destructorReturned{false};
    bool finishedWhenDestructorReturned = false; // пишет destroyer, читаем после join

    std::optional<StopFlag::Subscription> subscription;
    subscription.emplace(flag.onStop([&] {
        entered = true;
        while (!release) {
            std::this_thread::yield();
        }
        callbackFinished = true;
    }));

    std::thread stopper([&] { flag.requestStop(StopReason::Signal, SIGTERM); });
    const bool callbackEntered = waitUntil([&] { return entered.load(); }, milliseconds(5000));

    std::thread destroyer([&] {
        subscription.reset();
        finishedWhenDestructorReturned = callbackFinished.load();
        destructorReturned = true;
    });

    // Окно с ограничением по времени: пока колбэк не отпущен, деструктор не должен вернуться.
    // Корректная реализация проходит всегда; без ожидания деструктор вернётся почти сразу.
    const bool returnedTooEarly = waitUntil([&] { return destructorReturned.load(); }, milliseconds(100));

    release = true; // отпускаем колбэк в любом случае, чтобы потоки завершились
    destroyer.join();
    stopper.join();

    ASSERT_TRUE(callbackEntered) << "callback was never invoked";
    EXPECT_FALSE(returnedTooEarly) << "Subscription destructor returned while callback was running";
    EXPECT_TRUE(finishedWhenDestructorReturned);
}

// Config

TEST(ConfigTest, ParsesAllFields) {
    const auto config = parseConfig(R"({
        "database": {
            "host": "db.internal",
            "port": 6543,
            "name": "calc_db",
            "user": "calc_user",
            "password": "s3cret"
        }
    })");
    EXPECT_EQ(config.database.host, "db.internal");
    EXPECT_EQ(config.database.port, 6543);
    EXPECT_EQ(config.database.database, "calc_db");
    EXPECT_EQ(config.database.username, "calc_user");
    EXPECT_EQ(config.database.password, "s3cret");
}

TEST(ConfigTest, MissingKeysUseDefaults) {
    const DataBase::Config defaults;

    const auto empty = parseConfig("{}");
    EXPECT_EQ(empty.database.host, defaults.host);
    EXPECT_EQ(empty.database.port, defaults.port);
    EXPECT_EQ(empty.database.database, defaults.database);
    EXPECT_EQ(empty.database.username, defaults.username);
    EXPECT_EQ(empty.database.password, defaults.password);

    // Частичная секция: заданный ключ читается, остальные остаются по умолчанию.
    const auto partial = parseConfig(R"({"database": {"port": 5433}})");
    EXPECT_EQ(partial.database.port, 5433);
    EXPECT_EQ(partial.database.host, defaults.host);
    EXPECT_EQ(partial.database.password, defaults.password);
}

TEST(ConfigTest, ThrowsOnWrongType) {
    EXPECT_THROW(parseConfig(R"({"database": {"port": "abc"}})"), std::invalid_argument);
    EXPECT_THROW(parseConfig(R"({"database": {"host": 42}})"), std::invalid_argument);
    EXPECT_THROW(parseConfig(R"({"database": "localhost"})"), std::invalid_argument);
    EXPECT_THROW(parseConfig("[]"), std::invalid_argument);
}

TEST(ConfigTest, ThrowsOnPortOutOfRange) {
    EXPECT_THROW(parseConfig(R"({"database": {"port": 0}})"), std::invalid_argument);
    EXPECT_THROW(parseConfig(R"({"database": {"port": 65536}})"), std::invalid_argument);
    EXPECT_THROW(parseConfig(R"({"database": {"port": -1}})"), std::invalid_argument);
}

TEST(ConfigTest, ThrowsOnInvalidJson) {
    EXPECT_THROW(parseConfig("{not json"), std::invalid_argument);
    EXPECT_THROW(parseConfig(""), std::invalid_argument);
}
