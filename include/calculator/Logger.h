#pragma once

#include <string>

class Logger {
public:
    static Logger& instance();

    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger() = default;

    struct Impl;
    Impl* impl_;
};
