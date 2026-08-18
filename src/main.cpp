#include "calculator/Runner.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: calculator '<json>'\n"
                  << "Example: calculator '{\"op\":\"+\",\"a\":3,\"b\":4}'\n";
        return 1;
    }

    Runner runner;
    return runner.run(std::string(argv[1]));
}
