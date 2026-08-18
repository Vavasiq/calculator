#pragma once

#include <algorithm>
#include <string>

namespace calculator {

struct Task {
    int firstValue{0};
    int secondValue{0};
    char operation{'\0'};
    int result{0};
    int status{0};
};

inline Task normalizeTask(Task task) {
    if ((task.operation == '+' || task.operation == '*') && task.firstValue > task.secondValue) {
        std::swap(task.firstValue, task.secondValue);
    }
    return task;
}

inline std::string makeTaskKey(Task task) {
    task = normalizeTask(task);

    if (task.operation == '!') {
        return std::to_string(task.firstValue) + task.operation;
    }

    return std::to_string(task.firstValue) + task.operation + std::to_string(task.secondValue);
}

} // namespace calculator
