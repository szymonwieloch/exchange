#pragma once

#include <cassert>
#include <optional>
#include <thread>

#include "log.h"
#include "thread_utils.h"

namespace utils {

/// Lightweight thread wrapper with optional CPU pinning and naming.
///
/// The callback receives a const reference to the running flag so it can
/// poll for stop requests.  Errors during setup (core pinning / naming)
/// and unhandled exceptions are reported through the injected Logger.
class Thread {
public:
    Thread() = delete;

    /// @param name    Non-owning pointer to a thread name (15 chars max for Linux).
    /// @param logger  Used for warning/error messages during the thread lifetime.
    /// @param cpu     If set, the thread is pinned to this core.
    Thread(const char* name, Logger& logger, std::optional<int> cpu = {})
        : name_(name), logger_(logger), cpu_(cpu) {
        assert(name_);
    }

    /// Launches the thread.  Returns false if already running.
    template <typename T>
    [[nodiscard]] bool start(T&& callback) noexcept {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return false;
        }
        thread_ = std::thread(
            [this, cb = std::forward<T>(callback)]() { run(std::forward<decltype(cb)>(cb)); });
        return true;
    }

    /// Signals the thread to stop and joins it.
    void stop() noexcept {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    template <typename T>
    void run(T&& callback) noexcept {
        try {
            if (cpu_) {
                if (!setThreadCore(*cpu_)) {
                    logger_.warn("Could not set thread core of thread ", name_, ", core=", *cpu_);
                }
            }
            if (!setThreadName(name_)) {
                logger_.warn("Could not set name for thread ", name_);
            }
            callback(running_);
        } catch (const std::exception& e) {
            logger_.error("Unhandled std::exception in the thread ", name_);
        } catch (...) {
            logger_.error("Unhandled exception in the thread ", name_);
        }
        running_ = false;
    }

    const char* name_;
    Logger& logger_;
    std::optional<int> cpu_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace utils