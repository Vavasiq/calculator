#pragma once

#include <memory>
#include <string>

namespace calculator {

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
    ~Logger();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace calculator
