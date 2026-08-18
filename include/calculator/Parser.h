#pragma once

#include <string>

struct CalcData {
    int a{0};
    int b{0};
    char op{'\0'};
};

class Parser {
public:
    CalcData parse(const std::string& json_str) const;
};
