#include "calculator/WorkerThread.h"
#include "calculator/DataBase.h"
#include "calculator/Logger.h"
#include "calculator/Runner.h"
#include "calculator/Server.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace calculator {

WorkerThread::WorkerThread(StopFlag &stopFlag, AppConfig config)
    : stopFlag_(stopFlag), config_(std::move(config)), thread_(&WorkerThread::run, this) {}

WorkerThread::~WorkerThread() { join(); }

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
            // После старта config_ трогает только этот поток, поэтому его можно переместить.
            DataBase dataBase(std::move(config_.database));
            dataBase.connect();
            dataBase.warmUpCache();

            Runner runner(dataBase); // объявлен после dataBase -> уничтожается раньше неё

            // Возвращается после requestStop (SIGTERM/SIGINT через подписку -> io_context::stop()).
            runServer(config_.server, runner, stopFlag_,
                      [](const boost::asio::ip::tcp::endpoint &endpoint) {
                          Logger::instance().info("Listening on " + endpoint.address().to_string() +
                                                  ":" + std::to_string(endpoint.port()));
                      });

            Logger::instance().info("Server stopped");
        } // ~Runner, затем ~DataBase -> PQfinish

        Logger::instance().info("Database connection closed");
    } catch (const std::exception &e) {
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
