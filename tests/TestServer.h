#pragma once

#include "calculator/Runner.h"
#include "calculator/Server.h"
#include "calculator/StopFlag.h"
#include "calculator/TaskStorage.h"

#include "WaitUntil.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <string>
#include <thread>

namespace calculator::test {

// Сервер в отдельном потоке на том же runServer, что и сервис (рабочий поток), с собственным
// StopFlag и портом 0 (порт выбирает ОС): тесты не мешают друг другу при ctest -j и не
// конфликтуют с установленным сервисом на 7777. RAII: деструктор останавливает и join'ит поток.
//
// Всё, что тест читает из потока сервера, — атомики (port_, finished_) или читается после
// finished_ (error_): синхронизацию через сокет TSan не видит.
class TestServer {
  public:
    // Хранилищем не владеет: оно должно пережить TestServer.
    explicit TestServer(TaskStorage &storage) : runner_(storage), thread_([this] { run(); }) {}

    ~TestServer() { stop(); }

    TestServer(const TestServer &) = delete;
    TestServer &operator=(const TestServer &) = delete;

    // Busy-цикл с таймаутом до момента, когда сервер начал слушать (после listen).
    bool waitUntilListening(std::chrono::milliseconds timeout = kTestTimeout) const {
        return waitUntil([this] { return port_.load() != 0 || finished_.load(); }, timeout) &&
               port_.load() != 0;
    }

    std::uint16_t port() const { return port_.load(); }

    void requestStop() { stopFlag_.requestStop(StopReason::Signal, SIGTERM); }

    // Поток сервера вышел из runServer.
    bool finished() const { return finished_.load(); }

    // Текст исключения из потока сервера (пусто, если его не было или поток ещё работает).
    std::string error() const { return finished_.load() ? error_ : std::string{}; }

    void stop() {
        requestStop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

  private:
    void run() {
        try {
            runServer({"127.0.0.1", 0}, runner_, stopFlag_,
                      [this](const boost::asio::ip::tcp::endpoint &endpoint) {
                          port_ = endpoint.port();
                      });
        } catch (const std::exception &e) {
            error_ = e.what();
        } catch (...) {
            error_ = "unknown exception";
        }
        finished_ = true; // после записи error_: атомик публикует её потоку теста
    }

    Runner runner_;
    StopFlag stopFlag_;
    std::atomic<std::uint16_t> port_{0};
    std::atomic<bool> finished_{false};
    std::string error_;
    std::thread thread_; // последним: поток стартует, когда остальные поля готовы
};

} // namespace calculator::test
