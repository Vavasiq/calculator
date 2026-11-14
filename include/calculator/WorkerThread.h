#pragma once

#include "calculator/Config.h"
#include "calculator/StopFlag.h"

#include <thread>

namespace calculator {

// Рабочий поток: подключение к БД и прогрев кеша, затем TCP-сервер (runServer) до остановки.
// DataBase, Runner, io_context и сокеты живут только в этом потоке и уничтожаются в нём же:
// после остановки сокеты закрыты, соединение с БД закрыто (PQfinish).
class WorkerThread {
public:
    // config копируется в поле до старта потока и дальше используется только им.
    WorkerThread(StopFlag& stopFlag, AppConfig config);
    ~WorkerThread();

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;
    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    void join();

    // Только после join(): join задаёт happens-before для exitCode_.
    int exitCode() const;

private:
    void run();

    StopFlag& stopFlag_;
    AppConfig config_;
    int exitCode_{0};
    std::thread thread_; // последним: поток стартует, когда остальные поля готовы
};

} // namespace calculator
