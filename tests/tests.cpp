#include <gtest/gtest.h>

#include "calculator/Calculator.h"
#include "calculator/Checker.h"
#include "calculator/Config.h"
#include "calculator/Parser.h"
#include "calculator/Protocol.h"
#include "calculator/Runner.h"
#include "calculator/StopFlag.h"
#include "calculator/Task.h"

#include "InMemoryStorage.h"
#include "WaitUntil.h"

#include <mathlib/functions.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace calculator;
using calculator::test::FailingStorage;
using calculator::test::InMemoryStorage;
using calculator::test::waitUntil;

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

TEST(ParserTest, ThrowsOnIntOverflow) {
    // Раньше get<int>() молча превращал такие числа в другие.
    Parser p;
    EXPECT_THROW(p.parse(R"({"op":"+","a":3000000000,"b":1})"), std::invalid_argument);
    EXPECT_THROW(p.parse(R"({"op":"+","a":-3000000000,"b":1})"), std::invalid_argument);
    EXPECT_THROW(p.parse(R"({"op":"+","a":1,"b":2147483648})"), std::invalid_argument);
    EXPECT_THROW(p.parse(R"({"op":"+","a":18446744073709551615,"b":1})"), std::invalid_argument); // uint64 max
    EXPECT_THROW(p.parse(R"({"op":"+","a":1e30,"b":1})"), std::invalid_argument);                  // дробное
    EXPECT_THROW(p.parse(R"({"op":"!","a":99999999999})"), std::invalid_argument);
}

TEST(ParserTest, AcceptsIntBoundaries) {
    Parser p;
    const auto task = p.parse(R"({"op":"+","a":2147483647,"b":-2147483648})");
    EXPECT_EQ(task.firstValue, 2147483647);
    EXPECT_EQ(task.secondValue, -2147483647 - 1);
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

TEST(CalculatorTest, LargeFactorialOverflowsFast) {
    // Без защиты mathlib::fact ушла бы в рекурсию глубиной 10^8 и уронила процесс (стек).
    Calculator calc;
    const auto huge = calc.calculate({100000000, 0, '!'});
    EXPECT_EQ(huge.status, mathlib::MATH_OVERFLOW);
    EXPECT_EQ(huge.result, 0);

    // Граница: 12! помещается в int, 13! — нет.
    const auto twelve = calc.calculate({12, 0, '!'});
    EXPECT_EQ(twelve.status, mathlib::MATH_OK);
    EXPECT_EQ(twelve.result, 479001600);
    EXPECT_EQ(calc.calculate({13, 0, '!'}).status, mathlib::MATH_OVERFLOW);
    EXPECT_EQ(calc.calculate({0, 0, '!'}).result, 1);
}

TEST(CalculatorTest, PowerOfOneWithHugeExponentIsFast) {
    // Без защиты mathlib::powi крутил бы цикл 2·10^9 раз на каждый вызов — секунды.
    // Верхняя граница щедрая: с защитой это микросекунды даже под Valgrind.
    Calculator calc;
    const auto started = std::chrono::steady_clock::now();

    const auto one = calc.calculate({1, 2000000000, '^'});
    const auto zero = calc.calculate({0, 2000000000, '^'});
    const auto minusOneEven = calc.calculate({-1, 2000000000, '^'});
    const auto minusOneOdd = calc.calculate({-1, 2147483647, '^'});

    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(1));
    EXPECT_EQ(one.result, 1);
    EXPECT_EQ(zero.result, 0);
    EXPECT_EQ(minusOneEven.result, 1);
    EXPECT_EQ(minusOneOdd.result, -1);
    for (const auto& task : {one, zero, minusOneEven, minusOneOdd}) {
        EXPECT_EQ(task.status, mathlib::MATH_OK);
    }

    // Остальное поведение mathlib сохранено.
    EXPECT_EQ(calc.calculate({0, 0, '^'}).result, 1);
    EXPECT_EQ(calc.calculate({1, -1, '^'}).status, mathlib::MATH_DOMAIN);
    EXPECT_EQ(calc.calculate({2, 31, '^'}).status, mathlib::MATH_OVERFLOW);
    EXPECT_EQ(calc.calculate({-2, 31, '^'}).result, -2147483647 - 1); // INT_MIN помещается
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

TEST(ConfigTest, ParsesServerSection) {
    const auto config = parseConfig(R"({"server": {"address": "0.0.0.0", "port": 9000}})");
    EXPECT_EQ(config.server.address, "0.0.0.0");
    EXPECT_EQ(config.server.port, 9000);

    const auto ipv6 = parseConfig(R"({"server": {"address": "::1"}})");
    EXPECT_EQ(ipv6.server.address, "::1");
}

TEST(ConfigTest, ServerSectionDefaults) {
    const ServerConfig defaults;
    EXPECT_EQ(defaults.address, "127.0.0.1");
    EXPECT_EQ(defaults.port, 7777);

    const auto empty = parseConfig("{}");
    EXPECT_EQ(empty.server.address, defaults.address);
    EXPECT_EQ(empty.server.port, defaults.port);

    const auto partial = parseConfig(R"({"server": {"port": 7778}})");
    EXPECT_EQ(partial.server.address, defaults.address);
    EXPECT_EQ(partial.server.port, 7778);
}

TEST(ConfigTest, ThrowsOnInvalidServerSection) {
    EXPECT_THROW(parseConfig(R"({"server": {"address": "localhost"}})"), std::invalid_argument); // не IP
    EXPECT_THROW(parseConfig(R"({"server": {"address": "300.1.1.1"}})"), std::invalid_argument);
    EXPECT_THROW(parseConfig(R"({"server": {"address": 127}})"), std::invalid_argument);
    EXPECT_THROW(parseConfig(R"({"server": {"port": 0}})"), std::invalid_argument);
    EXPECT_THROW(parseConfig(R"({"server": {"port": "7777"}})"), std::invalid_argument);
    EXPECT_THROW(parseConfig(R"({"server": 7777})"), std::invalid_argument);
}

// Protocol

TEST(ProtocolTest, SerializesSuccess) {
    // Без поля error; ключи nlohmann выводит по алфавиту.
    EXPECT_EQ(serializeResponse({0, 5, ""}), R"({"result":5,"status":0})");
    EXPECT_EQ(serializeResponse({0, -7, "ignored"}), R"({"result":-7,"status":0})");
}

TEST(ProtocolTest, SerializesError) {
    EXPECT_EQ(serializeResponse({mathlib::MATH_DIV0, 0, "Division by zero"}),
              R"({"error":"Division by zero","result":0,"status":2})");
    EXPECT_EQ(serializeResponse({kStatusInvalidRequest, 0, "Missing or invalid field: 'b'"}),
              R"({"error":"Missing or invalid field: 'b'","result":0,"status":-1})");
    // Пустой текст ошибки -> подставляется statusMessage.
    EXPECT_EQ(serializeResponse({kStatusInternalError, 0, ""}),
              R"({"error":"Internal error","result":0,"status":-2})");
}

TEST(ProtocolTest, InvalidUtf8InErrorDoesNotThrow) {
    // Текст ошибки разбора содержит фрагмент ввода клиента — там может быть что угодно.
    std::string serialized;
    ASSERT_NO_THROW(serialized = serializeResponse({kStatusInvalidRequest, 0, "last read: '\xff'"}));
    EXPECT_NE(serialized.find("\xEF\xBF\xBD"), std::string::npos) << serialized; // U+FFFD
}

TEST(ProtocolTest, StatusMessages) {
    EXPECT_EQ(statusMessage(mathlib::MATH_OK), "OK");
    EXPECT_EQ(statusMessage(mathlib::MATH_OVERFLOW), "Integer overflow");
    EXPECT_EQ(statusMessage(mathlib::MATH_DIV0), "Division by zero");
    EXPECT_EQ(statusMessage(mathlib::MATH_DOMAIN), "Value out of domain");
    EXPECT_EQ(statusMessage(kStatusInvalidRequest), "Invalid request");
    EXPECT_EQ(statusMessage(kStatusInternalError), "Internal error");
    EXPECT_EQ(statusMessage(42), "Unknown error");
}

// Runner (без сети и без БД)

TEST(RunnerTest, ReturnsResult) {
    InMemoryStorage storage;
    Runner runner(storage);

    const auto response = runner.handle(R"({"op":"+","a":2,"b":3})");
    EXPECT_EQ(response.status, mathlib::MATH_OK);
    EXPECT_EQ(response.result, 5);
    EXPECT_TRUE(response.error.empty());

    EXPECT_EQ(runner.handle(R"({"op":"!","a":5})").result, 120);
}

TEST(RunnerTest, CommutativeRequestHitsCache) {
    InMemoryStorage storage;
    Runner runner(storage);

    EXPECT_EQ(runner.handle(R"({"op":"+","a":2,"b":3})").result, 5);
    EXPECT_EQ(runner.handle(R"({"op":"+","a":3,"b":2})").result, 5);
    EXPECT_EQ(storage.writes(), 1); // второй запрос взят из кеша

    // Вычитание не коммутативно: это отдельная запись.
    EXPECT_EQ(runner.handle(R"({"op":"-","a":3,"b":2})").result, 1);
    EXPECT_EQ(runner.handle(R"({"op":"-","a":2,"b":3})").result, -1);
    EXPECT_EQ(storage.writes(), 3);
}

TEST(RunnerTest, InvalidJsonGivesInvalidRequest) {
    InMemoryStorage storage;
    Runner runner(storage);

    for (const char* request : {"not json", "", R"({"op":"+","a":1})", R"({"op":"%","a":1,"b":2})"}) {
        const auto response = runner.handle(request);
        EXPECT_EQ(response.status, kStatusInvalidRequest) << request;
        EXPECT_EQ(response.result, 0) << request;
        EXPECT_FALSE(response.error.empty()) << request;
    }
    EXPECT_EQ(storage.writes(), 0); // некорректные запросы в хранилище не попадают
}

TEST(RunnerTest, DivisionByZeroGivesMathStatus) {
    InMemoryStorage storage;
    Runner runner(storage);

    for (int attempt = 0; attempt < 2; ++attempt) { // второй раз — из кеша
        const auto response = runner.handle(R"({"op":"/","a":5,"b":0})");
        EXPECT_EQ(response.status, mathlib::MATH_DIV0);
        EXPECT_EQ(response.result, 0);
        EXPECT_EQ(response.error, "Division by zero");
    }
    EXPECT_EQ(storage.writes(), 1); // ошибка вычисления тоже сохраняется (статус хранится в БД)
}

TEST(RunnerTest, InvalidUtf8RequestGivesSerializableResponse) {
    // Сырой байт 0xFF от клиента попадает в текст ошибки nlohmann ("last read: ...").
    // Ответ всё равно должен сериализоваться, иначе на шаге 3 это уронило бы сервер.
    InMemoryStorage storage;
    Runner runner(storage);

    for (const char* request : {"\xff", "{\"op\":\"\xff\",\"a\":1,\"b\":2}"}) {
        const auto response = runner.handle(request);
        EXPECT_EQ(response.status, kStatusInvalidRequest);
        EXPECT_NO_THROW(serializeResponse(response));
    }
}

TEST(RunnerTest, StorageFailureGivesInternalError) {
    FailingStorage storage;
    Runner runner(storage);

    const auto response = runner.handle(R"({"op":"*","a":6,"b":7})");
    EXPECT_EQ(response.status, kStatusInternalError);
    EXPECT_EQ(response.result, 0);
    EXPECT_EQ(response.error, "Internal error"); // без подробностей о БД
}
