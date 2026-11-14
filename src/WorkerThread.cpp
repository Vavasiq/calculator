#include "calculator/WorkerThread.h"
#include "calculator/Logger.h"
#include "calculator/Runner.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace calculator {

WorkerThread::WorkerThread(StopFlag& stopFlag, std::optional<std::string> initialTask)
    : stopFlag_(stopFlag), initialTask_(std::move(initialTask)), thread_(&WorkerThread::run, this) {}

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
            Runner runner;

            if (initialTask_) {
                runner.run(*initialTask_);
            }

            Logger::instance().info("Worker is waiting for stop request");
            stopFlag_.waitForStop();
            Logger::instance().info("Worker is shutting down");
        } // ~Runner -> ~DataBase -> PQfinish

        Logger::instance().info("Database connection closed");
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Worker failed: ") + e.what());
        std::cerr << "Error: " << e.what() << '\n';
        exitCode_ = 1;
        stopFlag_.requestStop(StopReason::Error);
    } catch (...) {
        Logger::instance().error("Worker failed: unknown exception");
        exitCode_ = 1;
        stopFlag_.requestStop(StopReason::Error);
    }
}

} // namespace calculator
