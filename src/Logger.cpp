#include "calculator/Logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace calculator {

struct Logger::Impl {
    std::shared_ptr<spdlog::logger> logger;

    Impl() {
        logger = spdlog::stdout_color_mt("calculator");
        logger->set_pattern("[%H:%M:%S] [%^%l%$] %v");
    }
};

Logger::Logger(): impl_(std::make_unique<Impl>()) {}

Logger::~Logger() = default;

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::info(const std::string& msg) {
    impl_->logger->info(msg);
}

void Logger::warn(const std::string& msg) {
    impl_->logger->warn(msg);
}

void Logger::error(const std::string& msg) {
    impl_->logger->error(msg);
}

} // namespace calculator
