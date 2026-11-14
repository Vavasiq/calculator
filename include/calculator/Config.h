#pragma once

#include "calculator/DataBase.h"

#include <string>

namespace calculator {

// Путь, по которому сервис ищет конфиг, если --config не задан.
inline constexpr const char* kDefaultConfigPath = "/etc/calculator/calculator.json";

// Конфигурация приложения верхнего уровня. Отдельная структура, чтобы в задаче 3
// добавить секцию server (порт) без изменения сигнатур.
struct AppConfig {
    DataBase::Config database;
};

// Разбирает JSON-текст конфига. Отсутствующие ключи берут значения по умолчанию
// из DataBase::Config, ключ с неверным типом или значением -> std::invalid_argument.
//
// Формат:
// {
//   "database": { "host": "...", "port": 5432, "name": "...", "user": "...", "password": "..." }
// }
AppConfig parseConfig(const std::string& json);

// Читает файл и разбирает его через parseConfig. Ошибки содержат путь к файлу:
// не удалось открыть -> std::runtime_error, неверное содержимое -> std::invalid_argument.
AppConfig loadConfig(const std::string& path);

} // namespace calculator
