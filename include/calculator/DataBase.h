#pragma once

#include "calculator/Postgres.h"
#include "calculator/Task.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace calculator {

class DataBase {
public:
    struct Config {
        std::string host{"localhost"};
        std::uint16_t port{5432};
        std::string database{"calculator"};
        std::string username{"calculator"};
        std::string password{"calculator"};
    };

    DataBase();
    explicit DataBase(Config config);
    ~DataBase();

    DataBase(const DataBase&) = delete;
    DataBase& operator=(const DataBase&) = delete;
    DataBase(DataBase&&) noexcept = default;
    DataBase& operator=(DataBase&&) noexcept = default;

    void connect();
    void disconnect();
    void warmUpCache();

    std::optional<Task> getRecord(Task task) const;
    void writeRecord(Task task);

private:
    void createTable();
    std::string connectionString() const;

    Config config_;
    PgConnection connection_;
    std::unordered_map<std::string, Task> cache_;
};

} // namespace calculator
