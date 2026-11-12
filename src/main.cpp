#include "calculator/Runner.h"

#include <csignal>
#include <exception>
#include <iostream>
#include <string>

namespace {

// Ждём SIGINT/SIGTERM, чтобы по Ctrl+C выйти штатным возвратом из main
// и дать деструкторам Runner/DataBase закрыть соединение (PQfinish).
// Сигналы сначала блокируются, потом принимаются вручную через sigwait:
// заблокированный сигнал не теряется, а ждёт разбора, поэтому гонки между
// проверкой флага и уходом в ожидание здесь нет (в отличие от pause()).
void waitForTerminationSignal() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    int signalNumber = 0;
    sigwait(&mask, &signalNumber);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: calculator '<json>'\n"
                  << "Example: calculator '{\"op\":\"+\",\"a\":3,\"b\":4}'\n";
        return 1;
    }

    try {
        calculator::Runner runner;
        const int status = runner.run(std::string(argv[1]));

        waitForTerminationSignal();

        return status;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}