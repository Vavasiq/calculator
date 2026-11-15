#include "calculator/StopFlag.h"
#include "calculator/Logger.h"

#include <algorithm>
#include <exception>
#include <string>

namespace calculator {

// --- Subscription ---

StopFlag::Subscription::Subscription(StopFlag& flag, std::uint64_t id) noexcept: flag_(flag), id_(id) {}

StopFlag::Subscription::~Subscription() {
    unsubscribe();
}

StopFlag::Subscription::Subscription(Subscription&& other) noexcept: flag_(other.flag_), id_(other.id_) {
    other.flag_.reset();
}

StopFlag::Subscription& StopFlag::Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        unsubscribe();
        flag_ = other.flag_;
        id_ = other.id_;
        other.flag_.reset();
    }
    return *this;
}

void StopFlag::Subscription::unsubscribe() {
    if (flag_) {
        flag_->get().unsubscribe(id_);
        flag_.reset();
    }
}

// --- StopFlag ---

void StopFlag::requestStop(StopReason reason, int signalNumber) {
    std::vector<std::pair<std::uint64_t, Callback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopRequested_) {
            return;
        }
        stopRequested_ = true;
        reason_ = reason;
        signalNumber_ = signalNumber;

        // Забираем колбэки под мьютексом: каждый будет вызван ровно один раз,
        // а подписка, сделанная после этого места, увидит stopRequested_ и вызовется сразу.
        callbacks.swap(callbacks_);
        if (!callbacks.empty()) {
            callbacksRunning_ = true;
            callbacksThread_ = std::this_thread::get_id();
        }

        // notify под мьютексом: ждущий поток не может проснуться, выйти и дать
        // уничтожить StopFlag до того, как мы закончим с cv_.
        cv_.notify_all();
    }

    if (callbacks.empty()) {
        return;
    }

    // Вне мьютекса: колбэк может сам обращаться к StopFlag без самоблокировки.
    for (const auto& entry : callbacks) {
        invokeCallback(entry.second);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacksRunning_ = false;
        // Будим деструкторы Subscription, которые ждут завершения колбэков.
        cv_.notify_all();
    }
    // callbacks (и захваченное в колбэках состояние) уничтожается здесь, уже вне мьютекса.
}

StopFlag::Subscription StopFlag::onStop(Callback callback) {
    if (!callback) {
        return Subscription{};
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Проверка флага и добавление в список — под одним захватом мьютекса:
        // requestStop не может проскочить между ними.
        if (!stopRequested_) {
            const std::uint64_t id = nextCallbackId_++;
            callbacks_.emplace_back(id, std::move(callback));
            return Subscription(*this, id);
        }
    }

    // Остановка уже запрошена: вызываем сразу, в этом потоке, вне мьютекса.
    invokeCallback(callback);
    return Subscription{};
}

void StopFlag::unsubscribe(std::uint64_t id) {
    // Объявлен до lock: удалённый колбэк уничтожится после освобождения мьютекса,
    // и деструкторы захваченных объектов не выполнятся под нашим мьютексом.
    Callback removed;
    std::unique_lock<std::mutex> lock(mutex_);

    const auto it = std::find_if(callbacks_.begin(), callbacks_.end(),
                                 [id](const auto& entry) { return entry.first == id; });
    if (it != callbacks_.end()) {
        removed = std::move(it->second);
        callbacks_.erase(it);
        return;
    }

    // Колбэка в списке нет — его уже забрал requestStop. Если он ещё выполняется в другом
    // потоке, ждём: после выхода из unsubscribe колбэк не должен трогать объекты подписчика.
    // В том же потоке (колбэк отписывает сам себя) не ждём — иначе самоблокировка.
    cv_.wait(lock, [this] { return !callbacksRunning_ || callbacksThread_ == std::this_thread::get_id(); });
}

void StopFlag::invokeCallback(const Callback& callback) noexcept {
    try {
        callback();
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Stop callback threw: ") + e.what());
    } catch (...) {
        Logger::instance().error("Stop callback threw an unknown exception");
    }
}

bool StopFlag::isStopRequested() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopRequested_;
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
