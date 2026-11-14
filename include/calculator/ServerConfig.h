#pragma once

#include <cstdint>
#include <string>

namespace calculator {

// Секция "server" конфига. Отдельный заголовок, чтобы Server.h не тянул DataBase/libpq.
struct ServerConfig {
    // По умолчанию только локальные подключения; "0.0.0.0" — слушать все интерфейсы.
    std::string address{"127.0.0.1"};
    // В конфиге 1..65535; 0 (порт выбирает ОС) используют только тесты.
    std::uint16_t port{7777};
};

} // namespace calculator
