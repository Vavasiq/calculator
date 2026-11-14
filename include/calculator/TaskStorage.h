#pragma once

#include "calculator/Task.h"

#include <optional>

namespace calculator {

// Хранилище результатов. Реализации: DataBase (PostgreSQL + кеш) и InMemoryStorage в тестах.
// Нужен, чтобы Runner и сервер можно было тестировать без живой БД.
class TaskStorage {
public:
    virtual ~TaskStorage() = default;

    // Задание из кеша или std::nullopt. Операнды коммутативных операций нормализуются внутри.
    virtual std::optional<Task> getRecord(Task task) const = 0;

    // Сохранить посчитанное задание.
    virtual void writeRecord(Task task) = 0;

protected:
    TaskStorage() = default;
    TaskStorage(const TaskStorage&) = default;
    TaskStorage& operator=(const TaskStorage&) = default;
    TaskStorage(TaskStorage&&) = default;
    TaskStorage& operator=(TaskStorage&&) = default;
};

} // namespace calculator
