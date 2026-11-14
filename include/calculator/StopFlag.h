#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace calculator {

enum class StopReason {
    None,   // остановка не запрошена
    Signal, // пришёл SIGINT/SIGTERM
    Error,  // рабочий поток завершился из-за ошибки
};

// Общий для потоков флаг остановки. Все поля читаются и пишутся только под mutex_,
// ожидание идёт через condition_variable с предикатом, поэтому запрос остановки,
// сделанный до начала ожидания, не теряется.
class StopFlag {
public:
    StopFlag() = default;

    // На объект ссылаются несколько потоков, перемещать его нельзя.
    StopFlag(const StopFlag&) = delete;
    StopFlag& operator=(const StopFlag&) = delete;
    StopFlag(StopFlag&&) = delete;
    StopFlag& operator=(StopFlag&&) = delete;

    // Идемпотентно: сохраняется причина первого вызова, повторные ничего не меняют.
    void requestStop(StopReason reason, int signalNumber = 0);

    bool isStopRequested() const;

    void waitForStop() const;

    // true, если остановка запрошена; false, если истёк таймаут.
    bool waitForStopFor(std::chrono::milliseconds timeout) const;

    StopReason reason() const;
    int signalNumber() const;

    // Сколько потоков сейчас находится внутри waitForStop/waitForStopFor.
    // Значение, прочитанное под mutex_, означает, что поток уже ждёт уведомления:
    // cv_.wait отпускает мьютекс атомарно с переходом в ожидание.
    std::size_t waitingThreads() const;

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    bool stopRequested_{false};
    StopReason reason_{StopReason::None};
    int signalNumber_{0};
    mutable std::size_t waitingThreads_{0};
};

} // namespace calculator
