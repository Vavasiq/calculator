#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace calculator {

enum class StopReason {
    None,   // остановка не запрошена
    Signal, // пришёл SIGINT/SIGTERM
    Error,  // рабочий поток завершился из-за ошибки
};

// Общий для потоков флаг остановки. Все поля читаются и пишутся только под mutex_,
// ожидание идёт через condition_variable с предикатом, поэтому запрос остановки,
// сделанный до начала ожидания, не теряется.
//
// Подписка на остановку (onStop): колбэк вызывается ровно один раз, всегда вне mutex_.
// Если остановка уже запрошена, колбэк вызывается сразу в потоке подписчика.
class StopFlag {
public:
    // RAII-подписка: деструктор отписывает колбэк. Если колбэк в этот момент выполняется
    // в другом потоке, деструктор ждёт его завершения (как std::stop_callback в C++20).
    // StopFlag должен пережить все свои подписки.
    class Subscription {
    public:
        Subscription() = default;
        ~Subscription();

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;

        // Отписаться досрочно. Повторный вызов и вызов у пустой подписки безопасны.
        void unsubscribe();

    private:
        friend class StopFlag;
        Subscription(StopFlag& flag, std::uint64_t id) noexcept;

        // Невладеющая ссылка на флаг; пустая — подписки нет (вызвана сразу, перемещена, отписана).
        std::optional<std::reference_wrapper<StopFlag>> flag_;
        std::uint64_t id_{0};
    };

    StopFlag() = default;

    // На объект ссылаются несколько потоков, перемещать его нельзя.
    StopFlag(const StopFlag&) = delete;
    StopFlag& operator=(const StopFlag&) = delete;
    StopFlag(StopFlag&&) = delete;
    StopFlag& operator=(StopFlag&&) = delete;

    // Идемпотентно: сохраняется причина первого вызова, повторные ничего не меняют.
    // Колбэки подписчиков вызываются из этого же вызова, вне mutex_.
    void requestStop(StopReason reason, int signalNumber = 0);

    // До requestStop колбэк сохраняется и будет вызван из requestStop.
    // После requestStop вызывается сразу, в вызывающем потоке, и возвращается пустая подписка.
    // Исключения из колбэка пишутся в лог и не распространяются.
    [[nodiscard]] Subscription onStop(std::function<void()> callback);

    bool isStopRequested() const;

    // Ожидание с ограничением по времени: true, если остановка запрошена; false, если истёк
    // таймаут. Сервис ждёт остановки в io_context.run() (подписка onStop), это ожидание нужно тестам.
    bool waitForStopFor(std::chrono::milliseconds timeout) const;

    StopReason reason() const;
    int signalNumber() const;

    // Сколько потоков сейчас находится внутри waitForStopFor (для детерминированных тестов).
    // Значение, прочитанное под mutex_, означает, что поток уже ждёт уведомления:
    // cv_.wait отпускает мьютекс атомарно с переходом в ожидание.
    std::size_t waitingThreads() const;

private:
    using Callback = std::function<void()>;

    void unsubscribe(std::uint64_t id);
    static void invokeCallback(const Callback& callback) noexcept;

    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    bool stopRequested_{false};
    StopReason reason_{StopReason::None};
    int signalNumber_{0};
    mutable std::size_t waitingThreads_{0};

    std::vector<std::pair<std::uint64_t, Callback>> callbacks_;
    std::uint64_t nextCallbackId_{1};
    // Колбэки, забранные requestStop, сейчас выполняются в потоке callbacksThread_.
    bool callbacksRunning_{false};
    std::thread::id callbacksThread_;
};

} // namespace calculator
