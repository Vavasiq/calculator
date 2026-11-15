#include "calculator/Server.h"
#include "calculator/Logger.h"
#include "calculator/Protocol.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/write.hpp>

#include <stdexcept>
#include <string>

namespace calculator {

namespace asio = boost::asio;
using asio::ip::tcp;

namespace {

std::string toString(const tcp::endpoint &endpoint) {
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

void throwIfFailed(const boost::system::error_code &error, const char *action,
                   const ServerConfig &config) {
    if (error) {
        throw std::runtime_error(std::string("Cannot ") + action + " " + config.address + ":" +
                                 std::to_string(config.port) + ": " + error.message());
    }
}

} // namespace

Server::Server(asio::io_context &io, const ServerConfig &config, Runner &runner)
    : runner_(runner), acceptor_(io), socket_(io), buffer_(kMaxRequestSize) {
    boost::system::error_code error;
    const auto address = asio::ip::make_address(config.address, error);
    throwIfFailed(error, "parse address", config);
    const tcp::endpoint endpoint(address, config.port);

    acceptor_.open(endpoint.protocol(), error);
    throwIfFailed(error, "open socket for", config);
    // Чтобы systemctl restart не упирался в "Address already in use" из-за TIME_WAIT.
    acceptor_.set_option(tcp::acceptor::reuse_address(true), error);
    throwIfFailed(error, "set SO_REUSEADDR on", config);
    acceptor_.bind(endpoint, error);
    throwIfFailed(error, "bind", config);
    acceptor_.listen(asio::socket_base::max_listen_connections, error);
    throwIfFailed(error, "listen on", config);

    startAccept();
}

tcp::endpoint Server::localEndpoint() const { return acceptor_.local_endpoint(); }

void Server::startAccept() {
    acceptor_.async_accept(socket_,
                           [this](const boost::system::error_code &error) { onAccept(error); });
}

void Server::onAccept(const boost::system::error_code &error) {
    if (error == asio::error::operation_aborted) {
        return; // остановка
    }
    if (error) {
        Logger::instance().warn("Accept failed: " + error.message());
        startAccept();
        return;
    }

    boost::system::error_code endpointError;
    const auto remote = socket_.remote_endpoint(endpointError);
    clientName_ = endpointError ? std::string("unknown") : toString(remote);
    Logger::instance().info("Client connected: " + clientName_);

    startRead();
}

void Server::startRead() {
    asio::async_read_until(socket_, buffer_, '\n',
                           [this](const boost::system::error_code &error, std::size_t bytes) {
                               onRead(error, bytes);
                           });
}

void Server::onRead(const boost::system::error_code &error, std::size_t bytes) {
    if (error == asio::error::operation_aborted) {
        return;
    }
    if (error == asio::error::not_found) {
        // Буфер заполнен (kMaxRequestSize), а '\n' так и нет.
        Logger::instance().warn("Request too long from " + clientName_);
        sendResponse(serializeResponse({kStatusInvalidRequest, 0, "Request too long"}), true);
        return;
    }
    if (error == asio::error::eof) {
        closeSession("Client disconnected");
        return;
    }
    if (error) {
        closeSession("Connection error (" + error.message() + ")");
        return;
    }

    // Ровно одна строка вместе с '\n'; остаток (pipelining) остаётся в буфере для следующего
    // чтения.
    const auto data = buffer_.data();
    std::string line(asio::buffers_begin(data),
                     asio::buffers_begin(data) + static_cast<std::ptrdiff_t>(bytes));
    buffer_.consume(bytes);

    line.pop_back(); // '\n'
    if (!line.empty() && line.back() == '\r') {
        line.pop_back(); // nc/telnet
    }

    sendResponse(serializeResponse(runner_.handle(line)), false);
}

void Server::sendResponse(const std::string &json, bool closeAfterWrite) {
    response_ = json + '\n';
    asio::async_write(
        socket_, asio::buffer(response_),
        [this, closeAfterWrite](const boost::system::error_code &error, std::size_t /*bytes*/) {
            onWrite(error, closeAfterWrite);
        });
}

void Server::onWrite(const boost::system::error_code &error, bool closeAfterWrite) {
    if (error == asio::error::operation_aborted) {
        return;
    }
    if (error) {
        // Например, клиент закрыл сокет до ответа: broken_pipe / connection_reset (не SIGPIPE).
        closeSession("Write failed (" + error.message() + ")");
        return;
    }
    if (closeAfterWrite) {
        boost::system::error_code ignored;
        socket_.shutdown(tcp::socket::shutdown_send, ignored); // клиент получит ответ и EOF
        startDrain();
        return;
    }
    startRead();
}

void Server::startDrain() {
    socket_.async_read_some(asio::buffer(drainBuffer_),
                            [this](const boost::system::error_code &error, std::size_t /*bytes*/) {
                                if (error == asio::error::operation_aborted) {
                                    return;
                                }
                                if (error) {
                                    closeSession("Connection closed after too long request");
                                    return;
                                }
                                startDrain(); // выбрасываем, пока клиент не закроет соединение
                            });
}

void Server::closeSession(const std::string &reason) {
    Logger::instance().info(reason + ": " + clientName_);

    boost::system::error_code ignored;
    socket_.shutdown(tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
    // Остаток данных предыдущего клиента не должен попасть следующему.
    buffer_.consume(buffer_.size());
    clientName_.clear();

    startAccept(); // один клиент за раз: следующего принимаем только сейчас
}

void runServer(const ServerConfig &config, Runner &runner, StopFlag &stopFlag,
               const ListeningCallback &onListening) {
    // Порядок важен (план, раздел 5): уничтожение в обратном порядке, поэтому подписка
    // снимается первой, и ни один колбэк уже не тронет io после выхода из функции.
    asio::io_context io;
    Server server(io, config, runner);

    if (onListening) {
        onListening(server.localEndpoint());
    }

    // io_context::stop() потокобезопасен — единственное, что поток сигналов делает с io.
    // Если остановка уже запрошена, колбэк вызовется сразу, и run() вернётся немедленно.
    const auto subscription = stopFlag.onStop([&io] { io.stop(); });

    io.run();
}

} // namespace calculator
