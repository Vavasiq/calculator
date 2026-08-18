#include "calculator/Postgres.h"

#include <stdexcept>
#include <vector>

namespace calculator {

PgResult::PgResult(PGresult* result): result_(result) {}

void PgResult::ResultDeleter::operator()(PGresult* result) const {
    if (result != nullptr) {
        PQclear(result);
    }
}

ExecStatusType PgResult::status() const {
    if (!result_) {
        throw std::runtime_error("PostgreSQL returned null result");
    }
    return PQresultStatus(result_.get());
}

int PgResult::rows() const {
    if (!result_) {
        return 0;
    }
    return PQntuples(result_.get());
}

const char* PgResult::value(int row, int column) const {
    if (!result_) {
        throw std::runtime_error("PostgreSQL returned null result");
    }
    return PQgetvalue(result_.get(), row, column);
}

const char* PgResult::errorMessage() const {
    if (!result_) {
        return "PostgreSQL returned null result";
    }
    return PQresultErrorMessage(result_.get());
}

void PgConnection::ConnectionDeleter::operator()(PGconn* connection) const {
    if (connection != nullptr) {
        PQfinish(connection);
    }
}

void PgConnection::connect(const std::string& connectionString) {
    disconnect();

    connection_.reset(PQconnectdb(connectionString.c_str()));
    if (!connection_) {
        throw std::runtime_error("Failed to allocate PostgreSQL connection");
    }

    if (PQstatus(connection_.get()) != CONNECTION_OK) {
        const std::string error = PQerrorMessage(connection_.get());
        disconnect();
        throw std::runtime_error("PostgreSQL connection failed: " + error);
    }
}

void PgConnection::disconnect() {
    connection_.reset();
}

bool PgConnection::isConnected() const {
    return connection_ && PQstatus(connection_.get()) == CONNECTION_OK;
}

PgResult PgConnection::execute(const std::string& query) const {
    if (!isConnected()) {
        throw std::runtime_error("PostgreSQL connection is not open");
    }

    return PgResult(PQexec(connection_.get(), query.c_str()));
}

PgResult PgConnection::executeParams(const std::string& query, const std::vector<std::string>& params) const {
    if (!isConnected()) {
        throw std::runtime_error("PostgreSQL connection is not open");
    }

    std::vector<const char*> values;
    values.reserve(params.size());
    for (const auto& param : params) {
        values.push_back(param.c_str());
    }

    return PgResult(PQexecParams(connection_.get(), query.c_str(), static_cast<int>(values.size()), nullptr,
                                 values.data(), nullptr, nullptr, 0));
}

} // namespace calculator
