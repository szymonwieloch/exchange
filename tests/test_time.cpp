/// @file test_time.cpp
/// @brief Unit tests for the TSC timestamp and calibration utilities.
///
/// Covers: TscTimestamp (construction, equality, relational, addition,
/// subtraction), readTsc() monotonicity, TscCalibration (default, calibrate,
/// toChrono with calibrated/uncalibrated state), and globalCalibration()
/// singleton lifetime.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <regex>
#include <sstream>
#include <string>

#include "lib/utils/time.h"

using utils::globalCalibration;
using utils::readTsc;
using utils::TscCalibration;
using utils::TscDuration;
using utils::TscTimestamp;

// =============================================================================
// TscTimestamp — construction and underlying value
// =============================================================================

TEST(TscTimestampTest, DefaultConstructsToZero) {
    const TscTimestamp ts{0};
    EXPECT_EQ(type_safe::get(ts), 0ULL);
}

TEST(TscTimestampTest, ConstructsFromUint64) {
    const TscTimestamp ts{42};
    EXPECT_EQ(type_safe::get(ts), 42ULL);
}

TEST(TscTimestampTest, MaxValueConstructible) {
    const TscTimestamp ts{UINT64_MAX};
    EXPECT_EQ(type_safe::get(ts), UINT64_MAX);
}

// =============================================================================
// TscTimestamp — equality comparison
// =============================================================================

TEST(TscTimestampTest, EqualForSameValue) {
    const TscTimestamp a{100};
    const TscTimestamp b{100};
    EXPECT_EQ(a, b);
}

TEST(TscTimestampTest, NotEqualForDifferentValues) {
    const TscTimestamp a{100};
    const TscTimestamp b{200};
    EXPECT_NE(a, b);
}

// =============================================================================
// TscTimestamp — relational comparison
// =============================================================================

TEST(TscTimestampTest, LessThan) {
    EXPECT_LT(TscTimestamp{10}, TscTimestamp{20});
}

TEST(TscTimestampTest, LessThanOrEqual) {
    EXPECT_LE(TscTimestamp{10}, TscTimestamp{10});
    EXPECT_LE(TscTimestamp{10}, TscTimestamp{20});
}

TEST(TscTimestampTest, GreaterThan) {
    EXPECT_GT(TscTimestamp{20}, TscTimestamp{10});
}

TEST(TscTimestampTest, GreaterThanOrEqual) {
    EXPECT_GE(TscTimestamp{20}, TscTimestamp{20});
    EXPECT_GE(TscTimestamp{20}, TscTimestamp{10});
}

// =============================================================================
// TscTimestamp — addition
// =============================================================================

TEST(TscTimestampTest, AdditionSameType) {
    const TscTimestamp a{100};
    const TscTimestamp b{200};
    const TscTimestamp sum = a + b;
    EXPECT_EQ(type_safe::get(sum), 300ULL);
}

TEST(TscTimestampTest, AdditionWithZero) {
    const TscTimestamp a{1234};
    const TscTimestamp result = a + TscTimestamp{0};
    EXPECT_EQ(result, a);
}

TEST(TscTimestampTest, AdditionWrapsOnOverflow) {
    const TscTimestamp a{UINT64_MAX};
    const TscTimestamp b{1};
    const TscTimestamp result = a + b;
    EXPECT_EQ(type_safe::get(result), 0ULL);  // wraps modulo 2^64
}

// =============================================================================
// TscTimestamp — subtraction → TscDuration
// =============================================================================

TEST(TscTimestampTest, SubtractionYieldsDuration) {
    const TscTimestamp a{500};
    const TscTimestamp b{200};
    const TscDuration diff = a - b;
    EXPECT_EQ(type_safe::get(diff), 300);
}

TEST(TscTimestampTest, SubtractionToZeroDuration) {
    const TscTimestamp a{100};
    const TscDuration result = a - TscTimestamp{100};
    EXPECT_EQ(type_safe::get(result), 0);
}

TEST(TscTimestampTest, SubtractionNegativeDuration) {
    const TscTimestamp a{0};
    const TscTimestamp b{1};
    const TscDuration result = a - b;
    EXPECT_EQ(type_safe::get(result), -1);
}

// =============================================================================
// readTsc() — basic sanity
// =============================================================================

TEST(ReadTscTest, ReturnsNonZero) {
    const TscTimestamp ts = readTsc();
    EXPECT_GT(type_safe::get(ts), 0ULL);
}

TEST(ReadTscTest, MonotonicallyIncreasing) {
    const TscTimestamp t0 = readTsc();
    const TscTimestamp t1 = readTsc();
    // t1 must be >= t0; on a quiet system it will almost always be strictly >.
    // We use >= to be robust against extremely fast consecutive reads.
    EXPECT_GE(t1, t0);
}

TEST(ReadTscTest, ReturnsDistinctValuesOverTime) {
    // Collect a handful of reads; at least some should differ.
    const TscTimestamp t0 = readTsc();
    bool any_different = false;
    for (int i = 0; i < 100; ++i) {
        if (readTsc() != t0) {
            any_different = true;
            break;
        }
    }
    EXPECT_TRUE(any_different) << "TSC did not advance across 100 reads";
}

// =============================================================================
// TscCalibration — default (uncalibrated) state
// =============================================================================

TEST(TscCalibrationTest, DefaultConstructorIsNotCalibrated) {
    const TscCalibration cal;
    EXPECT_FALSE(cal.calibrated());
}

TEST(TscCalibrationTest, ToChronoUncalibratedReturnsDefaultTimePoint) {
    const TscCalibration cal;
    const auto tp = cal.toChrono(TscTimestamp{42});
    EXPECT_EQ(tp, std::chrono::system_clock::time_point{});
}

// =============================================================================
// TscCalibration — calibrate()
// =============================================================================

TEST(TscCalibrationTest, CalibrateProducesCalibratedInstance) {
    const TscCalibration cal = TscCalibration::calibrate();
    EXPECT_TRUE(cal.calibrated());
}

TEST(TscCalibrationTest, CalibrateProducesNonZeroFrequency) {
    const TscCalibration cal = TscCalibration::calibrate();
    // On any real x86 CPU the TSC frequency will be well above zero.
    // We can't easily inspect the private member, but toChrono() behaves
    // correctly only when tsc_freq > 0.
    const auto tp = cal.toChrono(readTsc());
    EXPECT_NE(tp, std::chrono::system_clock::time_point{});
}

// =============================================================================
// TscCalibration — toChrono() after calibration
// =============================================================================

TEST(TscCalibrationTest, ToChronoReturnsReasonableWallClock) {
    const TscCalibration cal = TscCalibration::calibrate();
    const auto now = std::chrono::system_clock::now();
    const auto tp = cal.toChrono(readTsc());

    // The converted timestamp should be within 1 second of now.
    // (Calibration takes ~100 ms; the readTsc() fires right after.)
    const auto diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::abs(tp - now));
    EXPECT_LT(diff.count(), 1000) << "TSC→wall-clock conversion differs from system_clock by "
                                  << diff.count() << " ms";
}

TEST(TscCalibrationTest, LaterTscMapsToLaterWallClock) {
    const TscCalibration cal = TscCalibration::calibrate();
    const auto t0 = readTsc();
    // Busy-wait briefly so TSC advances measurably.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        _mm_pause();
    }
    const auto t1 = readTsc();
    ASSERT_GT(t1, t0);

    const auto tp0 = cal.toChrono(t0);
    const auto tp1 = cal.toChrono(t1);
    EXPECT_GT(tp1, tp0);
}

TEST(TscCalibrationTest, SameTscMapsToSameWallClock) {
    const TscCalibration cal = TscCalibration::calibrate();
    const TscTimestamp ts{42};
    const auto tp1 = cal.toChrono(ts);
    const auto tp2 = cal.toChrono(ts);
    EXPECT_EQ(tp1, tp2);
}

TEST(TscCalibrationTest, SmallTscDeltaMapsToSmallTimeDelta) {
    const TscCalibration cal = TscCalibration::calibrate();
    const TscTimestamp ts0{42};
    // A delta of 30 million ticks ≈ 10 ms on a 3 GHz CPU.
    const TscTimestamp ts1{type_safe::get(ts0) + 30'000'000ULL};
    const auto tp0 = cal.toChrono(ts0);
    const auto tp1 = cal.toChrono(ts1);

    const auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp1 - tp0);
    // Expect roughly 8–12 ms (allowing for a wide TSC frequency range).
    EXPECT_GE(delta_ms.count(), 1);
    EXPECT_LE(delta_ms.count(), 50);
}

// =============================================================================
// TscCalibration — multiple calibrations are independent
// =============================================================================

TEST(TscCalibrationTest, MultipleCalibrationsAreConsistent) {
    const TscCalibration cal1 = TscCalibration::calibrate();
    const TscCalibration cal2 = TscCalibration::calibrate();

    const auto now = readTsc();
    const auto tp1 = cal1.toChrono(now);
    const auto tp2 = cal2.toChrono(now);

    // Two calibrations of the same TSC should produce time_points within
    // a few ms of each other (different wall_clock_base times).
    const auto diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::abs(tp1 - tp2));
    EXPECT_LT(diff.count(), 500)  // 100 ms calibration + minor drift
        << "Two calibrations disagree by " << diff.count() << " ms";
}

// =============================================================================
// globalCalibration() — singleton
// =============================================================================

TEST(GlobalCalibrationTest, ReturnsReference) {
    const TscCalibration& cal = globalCalibration();
    EXPECT_TRUE(cal.calibrated());
}

TEST(GlobalCalibrationTest, AlwaysReturnsSameInstance) {
    const TscCalibration& cal1 = globalCalibration();
    const TscCalibration& cal2 = globalCalibration();
    EXPECT_EQ(&cal1, &cal2);
}

TEST(GlobalCalibrationTest, ConvertsRecentTscToReasonableTime) {
    const TscCalibration& cal = globalCalibration();
    const auto now = std::chrono::system_clock::now();
    const auto tp = cal.toChrono(readTsc());

    const auto diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::abs(tp - now));
    EXPECT_LT(diff.count(), 2000) << "Global calibration differs from system_clock by "
                                  << diff.count() << " ms";
}

// =============================================================================
// operator<< — TSC timestamp formatting
// =============================================================================
//
// NOTE: The format tests must call globalCalibration() before readTsc() to
// ensure the calibration baseline is established first.  Otherwise the
// captured TSC value predates the baseline, causing unsigned underflow in
// the tsc - tsc_base calculation.

TEST(TscTimestampFormatTest, OutputIsNonEmpty) {
    (void)globalCalibration();  // ensure calibration is complete
    std::ostringstream oss;
    oss << readTsc();
    EXPECT_FALSE(oss.str().empty());
}

TEST(TscTimestampFormatTest, OutputContainsExpectedFormat) {
    (void)globalCalibration();  // ensure calibration is complete
    std::ostringstream oss;
    oss << readTsc();
    const std::string s = oss.str();

    // The format is: "YYYY-MM-DD HH:MM:SS.nnnnnnnnn: "
    EXPECT_TRUE(
        std::regex_match(s, std::regex(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{9}: $)")))
        << "Unexpected format: '" << s << "' (len=" << s.size() << ")";
}

TEST(TscTimestampFormatTest, SameTimestampProducesSameString) {
    (void)globalCalibration();  // ensure calibration is complete
    const TscTimestamp ts = readTsc();
    std::ostringstream oss1;
    std::ostringstream oss2;
    oss1 << ts;
    oss2 << ts;
    EXPECT_EQ(oss1.str(), oss2.str());
}
