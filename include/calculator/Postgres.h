#pragma once

#include <libpq-fe.h>

#include <memory>
#include <string>
#include <vector>

namespace calculator {

class PgResult {
public:
    explicit PgResult(PGresult* result = nullptr);

    PgResult(const PgResult&) = delete;
    PgResult& operator=(const PgResult&) = delete;
    PgResult(PgResult&&) noexcept = default;
    PgResult& operator=(PgResult&&) noexcept = default;

    ExecStatusType status() const;
    int rows() const;
    const char* value(int row, int column) const;
    const char* errorMessage() const;

private:
    struct ResultDeleter {
        void operator()(PGresult* result) const;
    };

    std::unique_ptr<PGresult, ResultDeleter> result_;
};

class PgConnection {
public:
    PgConnection() = default;
    ~PgConnection() = default;

    PgConnection(const PgConnection&) = delete;
    PgConnection& operator=(const PgConnection&) = delete;
    PgConnection(PgConnection&&) noexcept = default;
    PgConnection& operator=(PgConnection&&) noexcept = default;

    void connect(const std::string& connectionString);
    void disconnect();
    bool isConnected() const;

    PgResult execute(const std::string& query) const;
    PgResult executeParams(const std::string& query, const std::vector<std::string>& params) const;

private:
    struct ConnectionDeleter {
        void operator()(PGconn* connection) const;
    };

    std::unique_ptr<PGconn, ConnectionDeleter> connection_;
};

} // namespace calculator
