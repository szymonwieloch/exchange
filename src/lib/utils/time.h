#pragma once

#include <x86intrin.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <type_safe/strong_typedef.hpp>

namespace utils {

struct TscTimestamp : type_safe::strong_typedef<TscTimestamp, std::uint64_t>,
                      type_safe::strong_typedef_op::equality_comparison<TscTimestamp>,
                      type_safe::strong_typedef_op::relational_comparison<TscTimestamp>,
                      type_safe::strong_typedef_op::integer_arithmetic<TscTimestamp> {
    using strong_typedef::strong_typedef;
};

inline TscTimestamp readTsc() noexcept {
    return TscTimestamp{static_cast<std::uint64_t>(__rdtsc())};
}

/// Calibrates the TSC against the system clock and converts TSC timestamps to
/// wall-clock time points.  Uses a process-wide singleton so that the Logger
/// and other consumers share a single calibration.
class TscCalibration {
public:
    TscCalibration() {}

    static TscCalibration calibrate() noexcept {
        TscCalibration result;
        constexpr auto calib = std::chrono::milliseconds(100);
        const auto t0 = readTsc();
        const auto c0 = std::chrono::high_resolution_clock::now();
        while (std::chrono::high_resolution_clock::now() - c0 < calib) {
        }
        const auto t1 = readTsc();
        const auto c1 = std::chrono::high_resolution_clock::now();
        const double elapsed_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(c1 - c0).count());
        result.tsc_freq = static_cast<std::uint64_t>(static_cast<double>(type_safe::get(t1 - t0)) *
                                                     1'000'000'000.0 / elapsed_ns);
        result.tsc_base = type_safe::get(t1);
        result.wall_clock_base = std::chrono::system_clock::now();
        return result;
    }

    [[nodiscard]] std::chrono::system_clock::time_point toChrono(TscTimestamp tsc) const noexcept {
        if (tsc_freq == 0) [[unlikely]] {
            return {};
        }
        const auto elapsed_ns = static_cast<std::uint64_t>(
            static_cast<double>(type_safe::get(tsc) - tsc_base) * 1'000'000'000.0 / tsc_freq);
        return wall_clock_base + std::chrono::nanoseconds(elapsed_ns);
    }

    [[nodiscard]] bool calibrated() const noexcept { return tsc_freq != 0; }

    inline void format(std::ostream &os, TscTimestamp timestamp) const noexcept {
        const auto tp = toChrono(timestamp);
        const auto time_t = std::chrono::system_clock::to_time_t(tp);
        const auto ns = static_cast<long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              tp.time_since_epoch() % std::chrono::seconds(1))
                                              .count());
        std::tm tm{};
        localtime_r(&time_t, &tm);
        os << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << '-' << std::setw(2)
           << (tm.tm_mon + 1) << '-' << std::setw(2) << tm.tm_mday << ' ' << std::setw(2)
           << tm.tm_hour << ':' << std::setw(2) << tm.tm_min << ':' << std::setw(2) << tm.tm_sec
           << '.' << std::setw(9) << ns << ": ";
    }

private:
    std::uint64_t tsc_freq = 0;  ///< TSC ticks per second (calibrated once).
    std::uint64_t tsc_base = 0;  ///< TSC value at calibration end (zero-reference).
    std::chrono::system_clock::time_point wall_clock_base =
        {};  ///< Wall-clock time at calibration.
};

}  // namespace utils