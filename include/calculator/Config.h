#pragma once

#include "calculator/DataBase.h"
#include "calculator/ServerConfig.h"

#include <string>

namespace calculator {

// Путь, по которому сервис ищет конфиг, если --config не задан.
inline constexpr const char* kDefaultConfigPath = "/etc/calculator/calculator.json";

// Конфигурация приложения верхнего уровня.
struct AppConfig {
    DataBase::Config database;
    ServerConfig server;
};

// Разбирает JSON-текст конфига. Отсутствующие ключи берут значения по умолчанию
// (DataBase::Config, ServerConfig), ключ с неверным типом или значением -> std::invalid_argument.
//
// Формат:
// {
//   "database": { "host": "...", "port": 5432, "name": "...", "user": "...", "password": "..." },
//   "server":   { "address": "127.0.0.1", "port": 7777 }
// }
AppConfig parseConfig(const std::string& json);

// Читает файл и разбирает его через parseConfig. Ошибки содержат путь к файлу:
// не удалось открыть -> std::runtime_error, неверное содержимое -> std::invalid_argument.
AppConfig loadConfig(const std::string& path);

} // namespace calculator
