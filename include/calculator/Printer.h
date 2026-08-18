#pragma once

#include "calculator/Task.h"

namespace calculator {

class Printer {
public:
    void printResult(const Task& task) const;
};

} // namespace calculator
