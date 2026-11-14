#pragma once

#include "calculator/Protocol.h"

#include "WaitUntil.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace calculator::test {

// Тестовый клиент к сервису на Boost.Asio (task4.md, раздел 5).
// Синхронный интерфейс поверх асинхронных операций с io_context::run_for(timeout),
// по образцу blocking_tcp_client из примеров Asio: если сервер не ответил за таймаут,
// операция отменяется и метод возвращает «неудачу» — тест падает, а не висит.
// Это не sleep: run_for возвращается, как только операция завершилась.
class TestClient {
public:
    explicit TestClient(std::chrono::milliseconds timeout = kTestTimeout);

    TestClient(const TestClient&) = delete;
    TestClient& operator=(const TestClient&) = delete;

    // Одна попытка подключения к 127.0.0.1:port. Готовность сервера ждёт TestServer.
    bool connect(std::uint16_t port);

    // Отправить байты как есть (без добавления '\n').
    bool sendRaw(const std::string& bytes);

    // Прочитать одну строку ответа без '\n'. std::nullopt — EOF, ошибка или таймаут (см. lastError).
    std::optional<std::string> readLine();

    // Прочитать строку и разобрать её как JSON-ответ протокола.
    std::optional<Response> readResponse();

    // sendRaw(line + '\n') + readResponse().
    std::optional<Response> request(const std::string& line);

    // true, если сервер закрыл соединение (EOF/reset) в пределах таймаута, не прислав данных.
    bool waitForServerClose();

    void close();

    // Описание последней ошибки — для сообщений в ASSERT/EXPECT.
    std::string lastError() const;

private:
    // Крутит io_ до завершения операции или таймаута; при таймауте закрывает сокет,
    // чтобы операция завершилась с operation_aborted.
    void run();

    std::chrono::milliseconds timeout_;
    boost::asio::io_context io_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::streambuf buffer_;
    std::string lastError_;
};

} // namespace calculator::test
