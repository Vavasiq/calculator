#include "calculator/Printer.h"
#include "calculator/Logger.h"

#include <iostream>
#include <string>

void Printer::printResult(int result) const {
    Logger::instance().info("Result: " + std::to_string(result));
    std::cout << result << "\n";
}
