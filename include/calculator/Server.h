#pragma once

#include "calculator/Runner.h"
#include "calculator/ServerConfig.h"
#include "calculator/StopFlag.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace calculator {

// TCP-сервер протокола (план: docs/task4-3-network.md, разделы 4 и 6).
// Обслуживает одного клиента за раз: следующий async_accept запускается только после
// конца текущей сессии. Все обработчики выполняются в потоке, который крутит io_context.
class Server {
  public:
    // open -> reuse_address -> bind -> listen. Ошибка (например, порт занят) — исключение
    // с адресом и портом в тексте.
    Server(boost::asio::io_context &io, const ServerConfig &config, Runner &runner);

    // Обработчики держат this, поэтому объект не копируется и не перемещается.
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;
    Server(Server &&) = delete;
    Server &operator=(Server &&) = delete;

    // Реальный адрес и порт (при port = 0 порт выбирает ОС). Вызывать из потока io_context
    // или до io.run().
    boost::asio::ip::tcp::endpoint localEndpoint() const;

  private:
    void startAccept();
    void onAccept(const boost::system::error_code &error);

    void startRead();
    void onRead(const boost::system::error_code &error, std::size_t bytes);

    void sendResponse(const std::string &json, bool closeAfterWrite);
    void onWrite(const boost::system::error_code &error, bool closeAfterWrite);

    // Аккуратное закрытие после «Request too long»: в сокете остались непрочитанные байты,
    // и обычный close() отправил бы RST, который может выбросить наш ответ. Поэтому:
    // shutdown(send) -> дочитать и выбросить остаток до EOF -> closeSession.
    void startDrain();

    void closeSession(const std::string &reason);

    Runner &runner_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::socket socket_;  // текущий клиент
    boost::asio::streambuf buffer_;        // max_size = kMaxRequestSize
    std::string response_;                 // живёт, пока идёт async_write
    std::array<char, 1024> drainBuffer_{}; // для startDrain
    std::string clientName_;               // адрес клиента для логов
};

using ListeningCallback = std::function<void(const boost::asio::ip::tcp::endpoint &)>;

// Общая функция для рабочего потока и тестов (план, раздел 6):
// io_context -> Server -> onListening(endpoint) -> подписка на StopFlag (io.stop()) -> io.run().
// Возвращается после requestStop. Если остановка уже запрошена, возвращается сразу.
void runServer(const ServerConfig &config, Runner &runner, StopFlag &stopFlag,
               const ListeningCallback &onListening);

} // namespace calculator
