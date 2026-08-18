#include "calculator/Runner.h"
#include "calculator/Calculator.h"
#include "calculator/Checker.h"
#include "calculator/Logger.h"
#include "calculator/Parser.h"
#include "calculator/Printer.h"

#include <iostream>
#include <stdexcept>

int Runner::run(const std::string& json_str) const {
    try {
        Parser parser;
        Checker checker;
        Calculator calculator;
        Printer printer;

        CalcData data = parser.parse(json_str);
        checker.check(data);
        int result = calculator.calculate(data);
        printer.printResult(result);

        return 0;
    } catch (const std::exception& e) {
        Logger::instance().error(e.what());
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
