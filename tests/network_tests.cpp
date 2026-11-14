// Сетевые тесты (план: docs/task4-3-network.md, раздел 11).
// Проверяется именно результат, полученный по сети: TestClient -> TCP -> runServer -> Runner.
// Нет sleep: готовность сервера и остановка ждутся busy-циклами с таймаутом (waitUntil),
// операции клиента ограничены таймаутом через io_context::run_for.
//
// SIGPIPE в тестовом бинарнике нарочно НЕ игнорируется: если сервер когда-нибудь
// получит SIGPIPE при записи в закрытый сокет, процесс тестов упадёт (раздел 8 плана).

#include <gtest/gtest.h>

#include "calculator/Config.h"
#include "calculator/DataBase.h"
#include "calculator/Protocol.h"
#include "calculator/Runner.h"
#include "calculator/Server.h"
#include "calculator/StopFlag.h"
#include "calculator/Task.h"

#include <mathlib/functions.h>

#include "InMemoryStorage.h"
#include "TestClient.h"
#include "TestServer.h"
#include "WaitUntil.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>

using namespace calculator;
using calculator::test::InMemoryStorage;
using calculator::test::kTestTimeout;
using calculator::test::TestClient;
using calculator::test::TestServer;
using calculator::test::waitUntil;

namespace {

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(server.waitUntilListening()) << "server did not start: " << server.error(); }

    // Порядок важен: server уничтожается первым (stop + join), затем storage.
    InMemoryStorage storage;
    TestServer server{storage};
};

} // namespace

TEST_F(ServerTest, ReturnsResultOverNetwork) {
    TestClient client;
    ASSERT_TRUE(client.connect(server.port())) << client.lastError();

    const auto response = client.request(R"({"op":"+","a":2,"b":3})");
    ASSERT_TRUE(response) << client.lastError();
    EXPECT_EQ(response->status, mathlib::MATH_OK);
    EXPECT_EQ(response->result, 5);
    EXPECT_TRUE(response->error.empty());
}

TEST_F(ServerTest, ReturnsMathErrorOverNetwork) {
    TestClient client;
    ASSERT_TRUE(client.connect(server.port())) << client.lastError();

    const auto response = client.request(R"({"op":"/","a":5,"b":0})");
    ASSERT_TRUE(response) << client.lastError();
    EXPECT_EQ(response->status, mathlib::MATH_DIV0);
    EXPECT_EQ(response->result, 0);
    EXPECT_EQ(response->error, "Division by zero");
}

TEST_F(ServerTest, InvalidRequestKeepsConnection) {
    TestClient client;
    ASSERT_TRUE(client.connect(server.port())) << client.lastError();

    const auto invalid = client.request("garbage");
    ASSERT_TRUE(invalid) << client.lastError();
    EXPECT_EQ(invalid->status, kStatusInvalidRequest);
    EXPECT_FALSE(invalid->error.empty());

    // То же соединение продолжает обслуживаться.
    const auto valid = client.request(R"({"op":"*","a":6,"b":7})");
    ASSERT_TRUE(valid) << client.lastError();
    EXPECT_EQ(valid->status, mathlib::MATH_OK);
    EXPECT_EQ(valid->result, 42);
}

TEST_F(ServerTest, SeveralRequestsOnOneConnection) {
    struct Case {
        const char* request;
        int result;
    };
    const Case cases[] = {
        {R"({"op":"+","a":7,"b":5})", 12},  {R"({"op":"-","a":7,"b":5})", 2},  {R"({"op":"*","a":7,"b":5})", 35},
        {R"({"op":"/","a":7,"b":5})", 1},   {R"({"op":"^","a":2,"b":10})", 1024}, {R"({"op":"!","a":5})", 120},
    };

    TestClient client;
    ASSERT_TRUE(client.connect(server.port())) << client.lastError();
    for (const auto& testCase : cases) {
        const auto response = client.request(testCase.request);
        ASSERT_TRUE(response) << testCase.request << ": " << client.lastError();
        EXPECT_EQ(response->status, mathlib::MATH_OK) << testCase.request;
        EXPECT_EQ(response->result, testCase.result) << testCase.request;
    }
}

TEST_F(ServerTest, PipelinedRequests) {
    TestClient client;
    ASSERT_TRUE(client.connect(server.port())) << client.lastError();

    // Две строки одним write, вторая — с CRLF (nc/telnet): два ответа в том же порядке.
    ASSERT_TRUE(client.sendRaw("{\"op\":\"*\",\"a\":6,\"b\":7}\n{\"op\":\"-\",\"a\":1,\"b\":10}\r\n")) << client.lastError();

    const auto first = client.readResponse();
    ASSERT_TRUE(first) << client.lastError();
    EXPECT_EQ(first->result, 42);

    const auto second = client.readResponse();
    ASSERT_TRUE(second) << client.lastError();
    EXPECT_EQ(second->result, -9);
}

TEST_F(ServerTest, ServesNextClientAfterDisconnect) {
    {
        TestClient first;
        ASSERT_TRUE(first.connect(server.port())) << first.lastError();
        const auto response = first.request(R"({"op":"+","a":1,"b":1})");
        ASSERT_TRUE(response) << first.lastError();
        EXPECT_EQ(response->result, 2);
    } // первый клиент отключился

    TestClient second;
    ASSERT_TRUE(second.connect(server.port())) << second.lastError();
    const auto response = second.request(R"({"op":"+","a":2,"b":2})");
    ASSERT_TRUE(response) << second.lastError();
    EXPECT_EQ(response->result, 4);
    EXPECT_EQ(storage.writes(), 2);
}

TEST_F(ServerTest, TooLongRequestClosesConnection) {
    TestClient client;
    ASSERT_TRUE(client.connect(server.port())) << client.lastError();

    ASSERT_TRUE(client.sendRaw(std::string(5000, 'x'))) << client.lastError(); // без '\n', больше kMaxRequestSize
    const auto response = client.readResponse();
    ASSERT_TRUE(response) << "response lost (RST instead of graceful close?): " << client.lastError();
    EXPECT_EQ(response->status, kStatusInvalidRequest);
    EXPECT_EQ(response->error, "Request too long");
    EXPECT_TRUE(client.waitForServerClose()) << client.lastError();
    client.close();

    // Сервер вернулся к приёму клиентов.
    TestClient next;
    ASSERT_TRUE(next.connect(server.port())) << next.lastError();
    const auto nextResponse = next.request(R"({"op":"+","a":3,"b":4})");
    ASSERT_TRUE(nextResponse) << next.lastError();
    EXPECT_EQ(nextResponse->result, 7);
}

TEST_F(ServerTest, SurvivesClientDisconnectBeforeResponse) {
    {
        // Много запросов одним пакетом и сразу закрыть сокет: сервер будет писать ответы
        // в уже закрытое соединение (EPIPE/ECONNRESET). Без MSG_NOSIGNAL это был бы SIGPIPE,
        // и тестовый процесс упал бы.
        TestClient client;
        ASSERT_TRUE(client.connect(server.port())) << client.lastError();
        std::string burst;
        for (int i = 0; i < 50; ++i) {
            burst += R"({"op":"+","a":)" + std::to_string(i) + R"(,"b":1})" + "\n";
        }
        ASSERT_TRUE(client.sendRaw(burst)) << client.lastError();
        client.close();
    }

    TestClient next;
    ASSERT_TRUE(next.connect(server.port())) << next.lastError();
    const auto response = next.request(R"({"op":"*","a":11,"b":11})");
    ASSERT_TRUE(response) << next.lastError();
    EXPECT_EQ(response->result, 121);
}

TEST_F(ServerTest, StopsOnStopRequest) {
    TestClient idle;
    ASSERT_TRUE(idle.connect(server.port())) << idle.lastError();
    const auto response = idle.request(R"({"op":"+","a":1,"b":2})");
    ASSERT_TRUE(response) << idle.lastError(); // сессия точно активна

    const auto port = server.port();
    server.requestStop();
    EXPECT_TRUE(waitUntil([this] { return server.finished(); }, kTestTimeout))
        << "server thread did not finish after requestStop";
    EXPECT_TRUE(server.error().empty()) << server.error();

    // Подключённый клиент видит закрытие, новые подключения отклоняются.
    EXPECT_TRUE(idle.waitForServerClose()) << idle.lastError();
    TestClient late;
    EXPECT_FALSE(late.connect(port)) << "server still accepts connections after stop";
}

TEST_F(ServerTest, HeavyInputsAnsweredOverNetwork) {
    // Входы, которые до защиты (раздел 13 плана) роняли или подвешивали сервис одним запросом.
    TestClient client;
    ASSERT_TRUE(client.connect(server.port())) << client.lastError();

    const auto factorial = client.request(R"({"op":"!","a":100000000})"); // раньше: переполнение стека
    ASSERT_TRUE(factorial) << client.lastError();
    EXPECT_EQ(factorial->status, mathlib::MATH_OVERFLOW);

    const auto power = client.request(R"({"op":"^","a":1,"b":2000000000})"); // раньше: секунды CPU
    ASSERT_TRUE(power) << client.lastError();
    EXPECT_EQ(power->status, mathlib::MATH_OK);
    EXPECT_EQ(power->result, 1);

    const auto overflow = client.request(R"({"op":"+","a":3000000000,"b":1})"); // раньше: молча другое число
    ASSERT_TRUE(overflow) << client.lastError();
    EXPECT_EQ(overflow->status, kStatusInvalidRequest);

    // Сервис жив и обслуживает дальше.
    const auto normal = client.request(R"({"op":"+","a":20,"b":22})");
    ASSERT_TRUE(normal) << client.lastError();
    EXPECT_EQ(normal->result, 42);
}

TEST(ServerStopTest, StopBeforeRunIsNotLost) {
    // Подписка после requestStop вызывается сразу: io.stop() до io.run() -> run() возвращается
    // немедленно. Так же сервис ведёт себя, если SIGTERM пришёл во время подключения к БД.
    InMemoryStorage storage;
    Runner runner(storage);
    StopFlag stopFlag;
    stopFlag.requestStop(StopReason::Signal, SIGTERM);

    std::atomic<bool> listened{false};
    std::atomic<bool> finished{false};
    std::thread serverThread([&] {
        runServer({"127.0.0.1", 0}, runner, stopFlag, [&](const boost::asio::ip::tcp::endpoint&) { listened = true; });
        finished = true;
    });

    // Если правило нарушено, runServer не вернётся, и тест оборвёт TIMEOUT ctest.
    EXPECT_TRUE(waitUntil([&] { return finished.load(); }, kTestTimeout)) << "runServer ignored an earlier stop";
    serverThread.join();
    EXPECT_TRUE(listened.load());
}

// --- Интеграционный тест с настоящей БД (раздел 11 плана) ---

TEST(IntegrationTest, RealDatabaseOverNetwork) {
    DataBase::Config config; // встроенные значения — те же, что у сервиса на ВМ
    if (const char* path = std::getenv("CALCULATOR_TEST_CONFIG")) {
        config = loadConfig(path).database;
    }

    // DataBase создаётся в потоке теста; пока сервер работает, ей пользуется только поток
    // сервера; после join (деструктор TestServer) она снова в потоке теста.
    DataBase dataBase(config);
    try {
        dataBase.connect();
        dataBase.warmUpCache();
    } catch (const std::exception& e) {
        GTEST_SKIP() << "PostgreSQL unavailable: " << e.what();
    }

    // Необычные числа, чтобы строку было легко узнать в psql; upsert -> повторные прогоны
    // новых строк не добавляют.
    const Task task{-424242, 1, '-'};
    {
        TestServer server(dataBase);
        ASSERT_TRUE(server.waitUntilListening()) << "server did not start: " << server.error();

        TestClient client;
        ASSERT_TRUE(client.connect(server.port())) << client.lastError();
        const auto response = client.request(R"({"op":"-","a":-424242,"b":1})");
        ASSERT_TRUE(response) << client.lastError();
        EXPECT_EQ(response->status, mathlib::MATH_OK);
        EXPECT_EQ(response->result, -424243);
    } // сервер остановлен, поток завершён

    // Результат действительно в PostgreSQL, а не только в кеше: независимое подключение
    // читает таблицу заново.
    DataBase check(config);
    check.connect();
    check.warmUpCache();
    const auto stored = check.getRecord(task);
    ASSERT_TRUE(stored.has_value()) << "task not found in calculation_history";
    EXPECT_EQ(stored->status, mathlib::MATH_OK);
    EXPECT_EQ(stored->result, -424243);
}
