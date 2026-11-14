#include "calculator/Config.h"
#include "calculator/Logger.h"
#include "calculator/SignalThread.h"
#include "calculator/StopFlag.h"
#include "calculator/WorkerThread.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace {

struct CommandLine {
    std::optional<std::string> configPath;
    std::optional<std::string> initialTask;
};

void printUsage() {
    std::cerr << "Usage: calculator [--config <path>] ['<json>']\n"
              << "Example: calculator '{\"op\":\"+\",\"a\":3,\"b\":4}'\n"
              << "Without a task runs as a service until SIGINT/SIGTERM.\n"
              << "Default config: " << calculator::kDefaultConfigPath
              << " (built-in defaults are used if it does not exist).\n";
}

// std::nullopt — аргументы некорректны.
std::optional<CommandLine> parseCommandLine(int argc, char** argv) {
    CommandLine commandLine;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc || commandLine.configPath) {
                return std::nullopt;
            }
            commandLine.configPath = argv[++i];
        } else if (arg.rfind("--", 0) == 0) {
            return std::nullopt; // неизвестная опция
        } else {
            if (commandLine.initialTask) {
                return std::nullopt;
            }
            commandLine.initialTask = arg;
        }
    }
    return commandLine;
}

// Явный --config: файл обязан существовать и быть корректным.
// Без --config: путь по умолчанию, а если файла нет — встроенные значения.
calculator::AppConfig resolveConfig(const std::optional<std::string>& explicitPath) {
    auto& logger = calculator::Logger::instance();

    if (explicitPath) {
        auto config = calculator::loadConfig(*explicitPath);
        logger.info("Config loaded from " + *explicitPath);
        return config;
    }

    std::error_code ec;
    if (!std::filesystem::exists(calculator::kDefaultConfigPath, ec)) {
        logger.warn(std::string("Config ") + calculator::kDefaultConfigPath + " not found, using built-in defaults");
        return calculator::AppConfig{};
    }

    auto config = calculator::loadConfig(calculator::kDefaultConfigPath);
    logger.info(std::string("Config loaded from ") + calculator::kDefaultConfigPath);
    return config;
}

void logStopReason(const calculator::StopFlag& stopFlag) {
    switch (stopFlag.reason()) {
        case calculator::StopReason::Signal:
            calculator::Logger::instance().info("Stopped by signal " + std::to_string(stopFlag.signalNumber()));
            break;
        case calculator::StopReason::Error: calculator::Logger::instance().error("Stopped due to an error"); break;
        case calculator::StopReason::None: break;
    }
}

} // namespace

int main(int argc, char** argv) {
    // До создания любых потоков: они унаследуют маску, и SIGINT/SIGTERM
    // получит только sigwait в потоке сигналов.
    try {
        calculator::SignalThread::blockTerminationSignals();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    auto commandLine = parseCommandLine(argc, argv);
    if (!commandLine) {
        printUsage();
        return 1;
    }

    // Конфиг читается до создания потоков: ошибка конфига -> выход без запуска потоков.
    calculator::AppConfig config;
    try {
        config = resolveConfig(commandLine->configPath);
    } catch (const std::exception& e) {
        calculator::Logger::instance().error(std::string("Config error: ") + e.what());
        return 1;
    }

    try {
        calculator::StopFlag stopFlag;
        calculator::SignalThread signals(stopFlag);
        calculator::WorkerThread worker(stopFlag, std::move(config.database), std::move(commandLine->initialTask));

        // Явный порядок, не зависящий от порядка деструкторов.
        worker.join();
        signals.stop();

        logStopReason(stopFlag);
        return worker.exitCode();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
