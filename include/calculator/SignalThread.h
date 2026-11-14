#pragma once

#include "calculator/StopFlag.h"

#include <thread>

namespace calculator {

// Поток, который синхронно принимает SIGINT/SIGTERM через sigwait и
// передаёт запрос остановки в StopFlag. RAII: деструктор будит и join'ит поток.
class SignalThread {
public:
    // Блокирует SIGINT/SIGTERM в вызывающем потоке. Вызывать первой строкой main,
    // до создания любых потоков: они унаследуют маску, и сигнал получит только sigwait.
    static void blockTerminationSignals();

    explicit SignalThread(StopFlag& stopFlag);
    ~SignalThread();

    SignalThread(const SignalThread&) = delete;
    SignalThread& operator=(const SignalThread&) = delete;
    SignalThread(SignalThread&&) = delete;
    SignalThread& operator=(SignalThread&&) = delete;

    // Если остановка пришла не от сигнала, будит поток из sigwait, затем join.
    // Повторный вызов безопасен.
    void stop();

private:
    void run();

    StopFlag& stopFlag_;
    std::thread thread_; // последним: поток стартует, когда остальные поля готовы
};

} // namespace calculator
