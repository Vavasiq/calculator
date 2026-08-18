#pragma once

#include "Parser.h"

class Calculator {
public:
    int calculate(const CalcData& data) const;
private:
    int checked(int code, int result) const;
};
