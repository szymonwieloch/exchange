#pragma once

#include <x86intrin.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <type_safe/strong_typedef.hpp>

namespace utils {

/// Strongly-typed wrapper representing a duration measured in TSC (Time Stamp
/// Counter) ticks. Uses a signed 64-bit integer so that negative durations
/// (when the subtrahend timestamp is later than the minuend) are representable.
///
/// This is the result type of subtracting two `TscTimestamp` values.
struct TscDuration : type_safe::strong_typedef<TscDuration, std::int64_t>,
                     type_safe::strong_typedef_op::equality_comparison<TscDuration>,
                     type_safe::strong_typedef_op::relational_comparison<TscDuration>,
                     type_safe::strong_typedef_op::addition<TscDuration>,
                     type_safe::strong_typedef_op::subtraction<TscDuration> {
    using strong_typedef::strong_typedef;
};

/// Strongly-typed wrapper around a raw x86 TSC (Time Stamp Counter) tick.
///
/// Wrapping the `uint64_t` in a distinct type prevents accidental mixing of
/// TSC values with other integer types (durations, price levels, etc.).
/// Addition of two timestamps is permitted for incrementing; subtraction of
/// two timestamps yields a `TscDuration` (see free-function `operator-`).
struct TscTimestamp
    : type_safe::strong_typedef<TscTimestamp, std::uint64_t>,
      type_safe::strong_typedef_op::equality_comparison<TscTimestamp>,
      type_safe::strong_typedef_op::relational_comparison<TscTimestamp>,
      type_safe::strong_typedef_op::addition<TscTimestamp>,
      type_safe::strong_typedef_op::mixed_addition<TscTimestamp, TscDuration>,
      type_safe::strong_typedef_op::mixed_subtraction_noncommutative<TscTimestamp, TscDuration> {
    using strong_typedef::strong_typedef;
};

/// Subtracting two absolute TSC timestamps yields a `TscDuration`, not
/// another timestamp. This free function overrides any same-type subtraction
/// that would otherwise return `TscTimestamp`.
inline TscDuration operator-(TscTimestamp lhs, TscTimestamp rhs) noexcept {
    return TscDuration{static_cast<std::int64_t>(type_safe::get(lhs) - type_safe::get(rhs))};
}

/// Reads the CPU's Time Stamp Counter (TSC) register.
///
/// Uses `__rdtsc()` with an `_mm_lfence()` barrier to prevent the CPU from
/// reordering the TSC read across surrounding instructions.  This is critical
/// for accurate calibration and benchmarking.  The combined sequence costs
/// ~30–40 cycles on modern x86 CPUs.
inline TscTimestamp readTsc() noexcept {
    _mm_lfence();
    return TscTimestamp{static_cast<std::uint64_t>(__rdtsc())};
}

/// Calibrates the TSC against the system clock and converts TSC timestamps to
/// wall-clock time points.  Uses a process-wide singleton so that the Logger
/// and other consumers share a single calibration.
class TscCalibration {
public:
    /// Default constructor.  Creates an uncalibrated instance
    /// (`tsc_freq == 0`).  Call `calibrate()` to obtain a usable
    /// calibration.
    TscCalibration() {}

    /// Performs a one-shot calibration of the TSC against the system clock.
    ///
    /// Busy-waits for ~100 ms while sampling both the TSC and
    /// `high_resolution_clock` to compute the TSC frequency (ticks/second).
    /// The returned instance can convert TSC values to wall-clock time
    /// with sub-microsecond accuracy.
    ///
    /// @note  This function blocks for the calibration duration (~100 ms)
    ///        and should be called once at startup, not in the hot path.
    static TscCalibration calibrate() noexcept {
        TscCalibration result;
        constexpr auto calib = std::chrono::milliseconds(100);
        const auto t0 = readTsc();
        const auto c0 = std::chrono::high_resolution_clock::now();
        while (std::chrono::high_resolution_clock::now() - c0 < calib) {
            _mm_pause();
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

    // GCC/Clang 128-bit integer — used for fixed-point TSC→ns conversion.
    // Wrapped in __extension__ to pass -Wpedantic.
    __extension__ typedef unsigned __int128 uint128_t;
    __extension__ typedef signed __int128 int128_t;

    /// Converts a TSC timestamp to a system_clock time_point.
    ///
    /// Uses pre-computed TSC frequency with 128-bit integer arithmetic to
    /// avoid floating-point.  Safe for non-hot-path use (called from the
    /// logger consumer thread, not the trading hot path).
    ///
    /// @param tsc  A TSC value previously obtained via `readTsc()`.
    /// @return The corresponding wall-clock time, or a default-constructed
    ///         time_point if the calibration has not been performed yet.
    [[nodiscard]] std::chrono::system_clock::time_point toChrono(TscTimestamp tsc) const noexcept {
        if (tsc_freq == 0) [[unlikely]] {
            return {};
        }
        const auto elapsed_ns = static_cast<std::uint64_t>(
            (static_cast<uint128_t>(type_safe::get(tsc) - tsc_base) * 1'000'000'000ULL) /
            static_cast<uint128_t>(tsc_freq));
        return wall_clock_base + std::chrono::nanoseconds(elapsed_ns);
    }

    /// Converts a TSC duration to a chrono::nanoseconds duration.
    ///
    /// Uses the same pre-computed TSC frequency as `toChrono(TscTimestamp)`,
    /// but returns a relative duration rather than an absolute time point.
    /// Handles negative durations correctly via signed 128-bit arithmetic.
    ///
    /// @param duration  A TSC duration (e.g. from subtracting two timestamps).
    /// @return The equivalent duration in nanoseconds, or zero if the
    ///         calibration has not been performed yet.
    [[nodiscard]] std::chrono::nanoseconds toChrono(TscDuration duration) const noexcept {
        if (tsc_freq == 0) [[unlikely]] {
            return std::chrono::nanoseconds{0};
        }
        const auto ns = static_cast<std::int64_t>(
            (static_cast<int128_t>(type_safe::get(duration)) * 1'000'000'000LL) /
            static_cast<int128_t>(tsc_freq));
        return std::chrono::nanoseconds{ns};
    }

    /// Returns `true` if `calibrate()` has been called and the TSC
    /// frequency is known (i.e., `tsc_freq != 0`).
    [[nodiscard]] bool calibrated() const noexcept { return tsc_freq != 0; }

private:
    std::uint64_t tsc_freq = 0;  ///< TSC ticks per second (calibrated once).
    std::uint64_t tsc_base = 0;  ///< TSC value at calibration end (zero-reference).
    std::chrono::system_clock::time_point wall_clock_base =
        {};  ///< Wall-clock time at calibration.
};

/// Returns the process-wide TSC calibration singleton.
///
/// Calibration occurs once on first access (thread-safe via C++11 static
/// local initialization).  All consumers — Logger, Market Data, etc. —
/// share the same calibration, ensuring consistent wall-clock timestamps
/// across the entire process.
inline TscCalibration& globalCalibration() noexcept {
    static TscCalibration instance = TscCalibration::calibrate();
    return instance;
}

/// Formats a TSC timestamp as a human-readable wall-clock string via the
/// global calibration singleton.
///
/// Output format: `YYYY-MM-DD HH:MM:SS.nnnnnnnnn` (ISO 8601 date, 24-hour
/// time with nanosecond precision).
inline std::ostream& operator<<(std::ostream& os, TscTimestamp timestamp) {
    const auto& cal = globalCalibration();
    const auto tp = cal.toChrono(timestamp);
    const auto time_t = std::chrono::system_clock::to_time_t(tp);
    const auto ns = static_cast<long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          tp.time_since_epoch() % std::chrono::seconds(1))
                                          .count());
    std::tm tm{};
    localtime_r(&time_t, &tm);
    os << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << '-' << std::setw(2)
       << (tm.tm_mon + 1) << '-' << std::setw(2) << tm.tm_mday << ' ' << std::setw(2) << tm.tm_hour
       << ':' << std::setw(2) << tm.tm_min << ':' << std::setw(2) << tm.tm_sec << '.'
       << std::setw(9) << ns << ": ";
    return os;
}

}  // namespace utils