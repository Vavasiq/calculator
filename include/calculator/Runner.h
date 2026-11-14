#pragma once

#include "calculator/DataBase.h"

#include <string>

namespace calculator {

class Runner {
public:
    explicit Runner(DataBase::Config config);
    ~Runner() = default;

    int run(const std::string& json_str);

private:
    DataBase dataBase_;
};

} // namespace calculator
