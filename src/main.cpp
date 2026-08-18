#include "calculator/Runner.h"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: calculator '<json>'\n"
                  << "Example: calculator '{\"op\":\"+\",\"a\":3,\"b\":4}'\n";
        return 1;
    }

    try {
        calculator::Runner runner;
        return runner.run(std::string(argv[1]));
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
