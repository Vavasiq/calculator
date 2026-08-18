#pragma once

#include "calculator/Task.h"

#include <string>

namespace calculator {

class Parser {
public:
    Task parse(const std::string& json_str) const;
};

} // namespace calculator
