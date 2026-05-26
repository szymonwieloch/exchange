#pragma once

#include <pthread.h>

#include <thread>

namespace utils {

/// Pins the calling thread to a specific CPU core (Linux-only).
///
/// Uses pthread_setaffinity_np which is a Linux/GNU extension. When running
/// on a non-Linux platform the function is still callable but returns false.
///
/// @param core_id  zero-based CPU core index to pin to.
/// @return true if affinity was successfully set, false otherwise (e.g.
///         invalid core id, insufficient privileges, or unsupported OS).
[[nodiscard]] inline bool setThreadCore(int core_id) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0);
}

}  // namespace utils