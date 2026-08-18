#include "calculator/Runner.h"
#include "calculator/Calculator.h"
#include "calculator/Checker.h"
#include "calculator/Logger.h"
#include "calculator/Parser.h"
#include "calculator/Printer.h"

#include <exception>
#include <iostream>

namespace calculator {

Runner::Runner() {
    dataBase_.connect();
    dataBase_.warmUpCache();
}

int Runner::run(const std::string& json_str) {
    try {
        Parser parser;
        Checker checker;
        Calculator calculator;
        Printer printer;

        Task task = parser.parse(json_str);
        checker.check(task);

        if (const auto cached = dataBase_.getRecord(task)) {
            task = *cached;
        } else {
            task = calculator.calculate(task);
            dataBase_.writeRecord(task);
        }

        printer.printResult(task);
        return task.status;
    } catch (const std::exception& e) {
        Logger::instance().error(e.what());
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}

} // namespace calculator
