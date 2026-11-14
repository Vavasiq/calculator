#pragma once

#include "calculator/Task.h"
#include "calculator/TaskStorage.h"

#include <atomic>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace calculator::test {

// Хранилище в памяти вместо PostgreSQL: тесты Runner и сервера не требуют живой БД
// и не засоряют calculation_history.
class InMemoryStorage : public TaskStorage {
public:
    std::optional<Task> getRecord(Task task) const override {
        const auto it = records_.find(makeTaskKey(normalizeTask(task)));
        if (it == records_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void writeRecord(Task task) override {
        task = normalizeTask(task);
        records_[makeTaskKey(task)] = task;
        ++writes_;
    }

    // Атомик: в сетевых тестах счётчик пишет поток сервера, а читает поток теста,
    // и синхронизацию через сокет TSan не видит.
    int writes() const { return writes_.load(); }

private:
    std::unordered_map<std::string, Task> records_;
    std::atomic<int> writes_{0};
};

// Хранилище, у которого ломается запись — как PostgreSQL, отвалившийся во время работы.
class FailingStorage : public TaskStorage {
public:
    std::optional<Task> getRecord(Task) const override { return std::nullopt; }
    void writeRecord(Task) override { throw std::runtime_error("PostgreSQL command failed: connection lost"); }
};

} // namespace calculator::test
