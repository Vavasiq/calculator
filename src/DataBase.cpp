#include "calculator/DataBase.h"
#include "calculator/Logger.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace calculator {

namespace {
void ensureCommandSucceeded(const PgResult& result) {
    if (result.status() != PGRES_COMMAND_OK) {
        throw std::runtime_error(std::string("PostgreSQL command failed: ") + result.errorMessage());
    }
}

void ensureQuerySucceeded(const PgResult& result) {
    if (result.status() != PGRES_TUPLES_OK) {
        throw std::runtime_error(std::string("PostgreSQL query failed: ") + result.errorMessage());
    }
}
} // namespace

DataBase::DataBase() = default;

DataBase::DataBase(Config config): config_(std::move(config)) {}

DataBase::~DataBase() {
    disconnect();
}

void DataBase::connect() {
    Logger::instance().info("Connecting to PostgreSQL");
    connection_.connect(connectionString());
    createTable();
}

void DataBase::disconnect() {
    connection_.disconnect();
}

void DataBase::warmUpCache() {
    Logger::instance().info("Warming up cache from PostgreSQL");

    const auto result = connection_.execute(
        "SELECT first_value, second_value, operation, result, status FROM calculation_history");
    ensureQuerySucceeded(result);

    cache_.clear();
    cache_.reserve(static_cast<std::size_t>(result.rows()));

    for (int row = 0; row < result.rows(); ++row) {
        Task task;
        task.firstValue = std::stoi(result.value(row, 0));
        task.secondValue = std::stoi(result.value(row, 1));

        const std::string operation = result.value(row, 2);
        if (operation.size() != 1) {
            throw std::runtime_error("Invalid operation stored in PostgreSQL");
        }
        task.operation = operation.front();

        task.result = std::stoi(result.value(row, 3));
        task.status = std::stoi(result.value(row, 4));

        task = normalizeTask(task);
        cache_[makeTaskKey(task)] = task;
    }

    Logger::instance().info("Cache warmed up, records: " + std::to_string(cache_.size()));
}

std::optional<Task> DataBase::getRecord(Task task) const {
    task = normalizeTask(task);
    const auto it = cache_.find(makeTaskKey(task));
    if (it == cache_.end()) {
        Logger::instance().info("Cache miss: " + makeTaskKey(task));
        return std::nullopt;
    }

    Logger::instance().info("Cache hit: " + makeTaskKey(task));
    return it->second;
}

void DataBase::writeRecord(Task task) {
    task = normalizeTask(task);

    const std::vector<std::string> params{
        std::to_string(task.firstValue),
        std::to_string(task.secondValue),
        std::string(1, task.operation),
        std::to_string(task.result),
        std::to_string(task.status),
    };

    const auto result = connection_.executeParams(
        "INSERT INTO calculation_history(first_value, second_value, operation, result, status) "
        "VALUES($1, $2, $3, $4, $5) "
        "ON CONFLICT(first_value, second_value, operation) DO UPDATE SET "
        "result = EXCLUDED.result, status = EXCLUDED.status",
        params);
    ensureCommandSucceeded(result);

    cache_[makeTaskKey(task)] = task;
    Logger::instance().info("Record saved: " + makeTaskKey(task));
}

void DataBase::createTable() {
    const auto result = connection_.execute(
        "CREATE TABLE IF NOT EXISTS calculation_history("
        "id BIGSERIAL PRIMARY KEY,"
        "first_value INTEGER NOT NULL,"
        "second_value INTEGER NOT NULL,"
        "operation CHAR(1) NOT NULL,"
        "result INTEGER NOT NULL,"
        "status INTEGER NOT NULL,"
        "UNIQUE(first_value, second_value, operation)"
        ")");
    ensureCommandSucceeded(result);
}

std::string DataBase::connectionString() const {
    return "host=" + config_.host + " port=" + std::to_string(config_.port) + " dbname=" + config_.database +
           " user=" + config_.username + " password=" + config_.password;
}

} // namespace calculator
