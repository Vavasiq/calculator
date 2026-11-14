#include "calculator/Logger.h"
#include "calculator/SignalThread.h"
#include "calculator/StopFlag.h"
#include "calculator/WorkerThread.h"

#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace {

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

    if (argc > 2) {
        std::cerr << "Usage: calculator ['<json>']\n"
                  << "Example: calculator '{\"op\":\"+\",\"a\":3,\"b\":4}'\n"
                  << "Without arguments runs as a service until SIGINT/SIGTERM.\n";
        return 1;
    }

    std::optional<std::string> initialTask;
    if (argc == 2) {
        initialTask = argv[1];
    }

    try {
        calculator::StopFlag stopFlag;
        calculator::SignalThread signals(stopFlag);
        calculator::WorkerThread worker(stopFlag, std::move(initialTask));

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
