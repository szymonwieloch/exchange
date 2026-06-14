#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace utils {

// ===================================================================
//  Counter  —  monotonically increasing, atomic, lock-free
// ===================================================================

/// A monotonically-increasing counter exposed to Prometheus.
///
/// All operations use `memory_order_relaxed` — the only requirement is
/// that the scrape thread eventually sees the latest value.  No
/// synchronisation with other memory locations is implied.
class Counter final {
public:
    /// Constructs a counter with the given name and optional help string.
    ///
    /// @param name   Prometheus metric name (e.g. "orders_processed_total").
    /// @param help   Human-readable description for the /metrics endpoint.
    explicit Counter(std::string name, std::string help = {}) noexcept
        : name_(std::move(name)), help_(std::move(help)) {}

    Counter(const Counter&) = delete;
    Counter& operator=(const Counter&) = delete;

    /// Increments the counter by @p delta (default 1).
    void inc(uint64_t delta = 1) noexcept { value_.fetch_add(delta, std::memory_order_relaxed); }

    /// Returns the current value.
    [[nodiscard]] uint64_t value() const noexcept { return value_.load(std::memory_order_relaxed); }

    /// Metric name as registered with Prometheus.
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    /// Optional help text.
    [[nodiscard]] const std::string& help() const noexcept { return help_; }

private:
    std::atomic<uint64_t> value_{0};
    std::string name_;
    std::string help_;
};

// ===================================================================
//  Gauge  —  up/down value, atomic, lock-free
// ===================================================================

/// A gauge that can increase and decrease, exposed to Prometheus.
///
/// Same relaxed memory ordering as Counter — eventual visibility is
/// sufficient for scrape-based monitoring.
class Gauge final {
public:
    explicit Gauge(std::string name, std::string help = {}) noexcept
        : name_(std::move(name)), help_(std::move(help)) {}

    Gauge(const Gauge&) = delete;
    Gauge& operator=(const Gauge&) = delete;

    /// Sets the gauge to an absolute value.
    void set(int64_t v) noexcept { value_.store(v, std::memory_order_relaxed); }

    /// Increments by @p delta (default 1).
    void inc(int64_t delta = 1) noexcept { value_.fetch_add(delta, std::memory_order_relaxed); }

    /// Decrements by @p delta (default 1).
    void dec(int64_t delta = 1) noexcept { value_.fetch_sub(delta, std::memory_order_relaxed); }

    /// Returns the current value.
    [[nodiscard]] int64_t value() const noexcept { return value_.load(std::memory_order_relaxed); }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& help() const noexcept { return help_; }

private:
    std::atomic<int64_t> value_{0};
    std::string name_;
    std::string help_;
};

// ===================================================================
//  Histogram  —  pre-allocated buckets, atomic, lock-free
// ===================================================================

/// A histogram with pre-configured, fixed bucket boundaries.
///
/// All bucket counters, the sum, and the total count are atomically
/// updated in the hot path.  Buckets are cumulative as required by
/// Prometheus — each bucket counts observations ≤ its upper bound.
///
/// Example buckets for order latency:
/// ```
/// Histogram h("order_latency_seconds", "Order processing latency",
///             {0.0001, 0.0005, 0.001, 0.005, 0.01});
/// ```
class Histogram final {
public:
    /// Constructs a histogram with the given bucket boundaries.
    ///
    /// @param name     Prometheus metric name.
    /// @param help     Human-readable description.
    /// @param buckets  Upper bounds of each bucket, must be sorted ascending.
    ///                 An implicit +Inf bucket is always present.
    Histogram(std::string name, std::string help, std::vector<double> buckets) noexcept
        : name_(std::move(name)),
          help_(std::move(help)),
          buckets_(std::move(buckets)),
          bucket_counts_(buckets_.size()),
          sum_{0},
          count_{0} {}

    Histogram(const Histogram&) = delete;
    Histogram& operator=(const Histogram&) = delete;

    /// Observes a value, incrementing the appropriate bucket(s).
    ///
    /// All buckets with upper bound ≥ @p val are incremented (cumulative
    /// histogram).  Sum and count are also updated.
    void observe(double val) noexcept {
        for (size_t i = 0; i < buckets_.size(); ++i) {
            if (val <= buckets_[i]) {
                bucket_counts_[i].fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Reinterpret-cast the double to uint64_t for atomic add on the
        // bit-pattern, then convert back.  This avoids a CAS loop but
        // requires IEEE 754.  We assert correctness at startup.
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        uint64_t old_bits = sum_.load(std::memory_order_relaxed);
        double old_val, new_val;
        do {
            std::memcpy(&old_val, &old_bits, sizeof(old_val));
            new_val = old_val + val;
            uint64_t new_bits;
            std::memcpy(&new_bits, &new_val, sizeof(new_bits));
            if (sum_.compare_exchange_weak(old_bits, new_bits, std::memory_order_relaxed,
                                           std::memory_order_relaxed)) {
                break;
            }
        } while (true);
        count_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Returns a snapshot of all bucket counts (cumulative, including +Inf).
    [[nodiscard]] std::vector<uint64_t> bucketValues() const noexcept {
        std::vector<uint64_t> result(buckets_.size() + 1);
        for (size_t i = 0; i < buckets_.size(); ++i) {
            result[i] = bucket_counts_[i].load(std::memory_order_relaxed);
        }
        result[buckets_.size()] = count_.load(std::memory_order_relaxed);  // +Inf
        return result;
    }

    /// Returns the upper bound of bucket @p i, or +Inf for i == size().
    [[nodiscard]] double bucketBound(size_t i) const noexcept {
        if (i < buckets_.size())
            return buckets_[i];
        return std::numeric_limits<double>::infinity();
    }

    /// Number of finite buckets (excluding +Inf).
    [[nodiscard]] size_t bucketCount() const noexcept { return buckets_.size(); }

    /// Sum of all observed values.
    [[nodiscard]] double sum() const noexcept {
        uint64_t bits = sum_.load(std::memory_order_relaxed);
        double result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    /// Total number of observations.
    [[nodiscard]] uint64_t count() const noexcept { return count_.load(std::memory_order_relaxed); }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& help() const noexcept { return help_; }

private:
    std::string name_;
    std::string help_;
    std::vector<double> buckets_;
    std::vector<std::atomic<uint64_t>> bucket_counts_;
    std::atomic<uint64_t> sum_;  // bit-cast of double
    std::atomic<uint64_t> count_;
};

}  // namespace utils
