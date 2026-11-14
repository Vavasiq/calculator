#include "calculator/StopFlag.h"

namespace calculator {

void StopFlag::requestStop(StopReason reason, int signalNumber) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopRequested_) {
        return;
    }
    stopRequested_ = true;
    reason_ = reason;
    signalNumber_ = signalNumber;

    // notify под мьютексом: ждущий поток не может проснуться, выйти и дать
    // уничтожить StopFlag до того, как мы закончим с cv_.
    cv_.notify_all();
}

bool StopFlag::isStopRequested() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopRequested_;
}

void StopFlag::waitForStop() const {
    std::unique_lock<std::mutex> lock(mutex_);
    ++waitingThreads_;
    cv_.wait(lock, [this] { return stopRequested_; });
    --waitingThreads_;
}

bool StopFlag::waitForStopFor(std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    ++waitingThreads_;
    const bool stopped = cv_.wait_for(lock, timeout, [this] { return stopRequested_; });
    --waitingThreads_;
    return stopped;
}

StopReason StopFlag::reason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reason_;
}

int StopFlag::signalNumber() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return signalNumber_;
}

std::size_t StopFlag::waitingThreads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return waitingThreads_;
}

} // namespace calculator
