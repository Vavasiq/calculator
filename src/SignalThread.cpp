#include "calculator/SignalThread.h"
#include "calculator/Logger.h"

#include <pthread.h>
#include <signal.h>

#include <cstring>
#include <exception>
#include <string>
#include <system_error>

namespace calculator {

namespace {

// Единственное место, где задан набор сигналов: и для маски, и для sigwait.
sigset_t terminationSignals() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    return set;
}

std::string signalName(int signalNumber) {
    switch (signalNumber) {
        case SIGINT: return "SIGINT";
        case SIGTERM: return "SIGTERM";
        default: return "signal " + std::to_string(signalNumber);
    }
}

} // namespace

void SignalThread::blockTerminationSignals() {
    const sigset_t set = terminationSignals();
    const int rc = pthread_sigmask(SIG_BLOCK, &set, nullptr);
    if (rc != 0) {
        throw std::system_error(rc, std::generic_category(), "pthread_sigmask");
    }
}

SignalThread::SignalThread(StopFlag& stopFlag): stopFlag_(stopFlag), thread_(&SignalThread::run, this) {}

SignalThread::~SignalThread() {
    stop();
}

void SignalThread::stop() {
    if (!thread_.joinable()) {
        return;
    }

    // Остановку запросил не сигнал (ошибка в рабочем потоке): поток всё ещё в sigwait
    // и сам не проснётся. Сигнал заблокирован и адресован этому потоку, его заберёт sigwait.
    // pthread_t валиден до join, даже если поток уже завершился.
    if (stopFlag_.reason() != StopReason::Signal) {
        pthread_kill(thread_.native_handle(), SIGTERM);
    }

    thread_.join();
}

void SignalThread::run() {
    try {
        Logger::instance().info("Signal thread started, waiting for SIGINT/SIGTERM");

        const sigset_t set = terminationSignals();
        int signalNumber = 0;
        const int rc = sigwait(&set, &signalNumber);
        if (rc != 0) {
            Logger::instance().error(std::string("sigwait failed: ") + std::strerror(rc));
            stopFlag_.requestStop(StopReason::Error);
            return;
        }

        if (stopFlag_.isStopRequested()) {
            Logger::instance().info("Signal thread woken up for shutdown");
        } else {
            Logger::instance().info("Received " + signalName(signalNumber) + ", requesting stop");
        }
        stopFlag_.requestStop(StopReason::Signal, signalNumber);
    } catch (...) {
        // Исключение из функции потока вызвало бы std::terminate без деструкторов.
        stopFlag_.requestStop(StopReason::Error);
    }
}

} // namespace calculator
