#pragma once

#include "calculator/DataBase.h"
#include "calculator/StopFlag.h"

#include <optional>
#include <string>
#include <thread>

namespace calculator {

// Рабочий поток: создаёт Runner (подключение к БД, прогрев кеша), обрабатывает
// начальное задание и ждёт остановки. Runner и все объекты БД живут только в этом
// потоке и уничтожаются в нём же, после чего соединение закрыто (PQfinish).
class WorkerThread {
public:
    // dbConfig копируется в поле до старта потока и дальше используется только им.
    WorkerThread(StopFlag& stopFlag, DataBase::Config dbConfig, std::optional<std::string> initialTask);
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
    DataBase::Config dbConfig_;
    std::optional<std::string> initialTask_;
    int exitCode_{0};
    std::thread thread_; // последним: поток стартует, когда остальные поля готовы
};

} // namespace calculator
