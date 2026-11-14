#include "calculator/WorkerThread.h"
#include "calculator/Logger.h"
#include "calculator/Runner.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace calculator {

WorkerThread::WorkerThread(StopFlag& stopFlag, DataBase::Config dbConfig, std::optional<std::string> initialTask)
    : stopFlag_(stopFlag), dbConfig_(std::move(dbConfig)), initialTask_(std::move(initialTask)),
      thread_(&WorkerThread::run, this) {}

WorkerThread::~WorkerThread() {
    join();
}

void WorkerThread::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

int WorkerThread::exitCode() const {
    if (thread_.joinable()) {
        throw std::logic_error("WorkerThread::exitCode() called before join()");
    }
    return exitCode_;
}

void WorkerThread::run() {
    try {
        {
            // После старта dbConfig_ трогает только этот поток, поэтому его можно переместить.
            Runner runner(std::move(dbConfig_));

            if (initialTask_) {
                runner.run(*initialTask_);
            }

            Logger::instance().info("Worker is waiting for stop request");
            stopFlag_.waitForStop();
            Logger::instance().info("Worker is shutting down");
        } // ~Runner -> ~DataBase -> PQfinish

        Logger::instance().info("Database connection closed");
    } catch (const std::exception& e) {
        // Только логгер: он и так пишет в stdout, и под systemd второй вывод в stderr
        // давал бы в журнале ту же ошибку дважды.
        Logger::instance().error(std::string("Worker failed: ") + e.what());
        exitCode_ = 1;
        stopFlag_.requestStop(StopReason::Error);
    } catch (...) {
        Logger::instance().error("Worker failed: unknown exception");
        exitCode_ = 1;
        stopFlag_.requestStop(StopReason::Error);
    }
}

} // namespace calculator
