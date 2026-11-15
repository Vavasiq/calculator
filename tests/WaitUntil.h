#pragma once

#include <chrono>
#include <thread>

namespace calculator::test {

// Busy-цикл с ограничением по времени вместо sleep (требование task4.md, раздел 5):
// true, если условие выполнилось до дедлайна.
template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

// Щедрый таймаут: memcheck прогоняет те же тесты под Valgrind, а он в 10–50 раз медленнее.
inline constexpr std::chrono::milliseconds kTestTimeout{5000};

} // namespace calculator::test
