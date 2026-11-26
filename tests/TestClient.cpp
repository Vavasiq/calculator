#include "TestClient.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <exception>

namespace calculator::test {

namespace asio = boost::asio;
using asio::ip::tcp;

TestClient::TestClient(std::chrono::milliseconds timeout) : timeout_(timeout), socket_(io_) {}

void TestClient::run() {
    io_.restart();
    io_.run_for(timeout_);
    if (!io_.stopped()) {
        // Таймаут: закрываем сокет, и незавершённая операция получит operation_aborted.
        boost::system::error_code ignored;
        socket_.close(ignored);
        io_.run();
        lastError_ = "timeout after " + std::to_string(timeout_.count()) + " ms";
    }
}

bool TestClient::connect(std::uint16_t port) {
    boost::system::error_code result = asio::error::would_block;
    const tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), port);
    socket_.async_connect(endpoint,
                          [&result](const boost::system::error_code &error) { result = error; });
    run();
    if (result) {
        if (result != asio::error::operation_aborted) {
            lastError_ = "connect: " + result.message();
        }
        return false;
    }
    return true;
}

bool TestClient::sendRaw(const std::string &bytes) {
    boost::system::error_code result = asio::error::would_block;
    asio::async_write(
        socket_, asio::buffer(bytes),
        [&result](const boost::system::error_code &error, std::size_t) { result = error; });
    run();
    if (result) {
        if (result != asio::error::operation_aborted) {
            lastError_ = "write: " + result.message();
        }
        return false;
    }
    return true;
}

std::optional<std::string> TestClient::readLine() {
    boost::system::error_code result = asio::error::would_block;
    std::size_t bytes = 0;
    asio::async_read_until(
        socket_, buffer_, '\n',
        [&result, &bytes](const boost::system::error_code &error, std::size_t n) {
            result = error;
            bytes = n;
        });
    run();
    if (result) {
        if (result != asio::error::operation_aborted) {
            lastError_ = "read: " + result.message();
        }
        return std::nullopt;
    }

    const auto data = buffer_.data();
    std::string line(asio::buffers_begin(data),
                     asio::buffers_begin(data) + static_cast<std::ptrdiff_t>(bytes));
    buffer_.consume(bytes);
    line.pop_back(); // '\n'
    return line;
}

std::optional<Response> TestClient::readResponse() {
    const auto line = readLine();
    if (!line) {
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(*line);
        Response response;
        response.status = json.at("status").get<int>();
        response.result = json.at("result").get<int>();
        if (json.contains("error")) {
            response.error = json.at("error").get<std::string>();
        }
        return response;
    } catch (const std::exception &e) {
        lastError_ = "bad response '" + *line + "': " + e.what();
        return std::nullopt;
    }
}

std::optional<Response> TestClient::request(const std::string &line) {
    if (!sendRaw(line + "\n")) {
        return std::nullopt;
    }
    return readResponse();
}

bool TestClient::waitForServerClose() {
    const auto line = readLine();
    if (line) {
        lastError_ = "expected connection close, got data: " + *line;
        return false;
    }
    // readLine записал причину: read: End of file / Connection reset by peer / timeout
    return lastError_.rfind("read: ", 0) == 0;
}

void TestClient::close() {
    boost::system::error_code ignored;
    socket_.close(ignored);
}

std::string TestClient::lastError() const { return lastError_; }

} // namespace calculator::test
