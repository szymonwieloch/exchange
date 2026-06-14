#pragma once

#include "metrics.h"
#include "time.h"

namespace utils {

using ProfilingStats = Histogram<std::chrono::nanoseconds>;

template <typename MeasurementId>
class Profiler {
public:
    Profiler(ProfilingStats& stats) : start(readTsc()), stats(stats) {}
    ~Profiler() {
        TscDuration diff = readTsc() - start;
        stats.observe(globalCalibration().toChrono(diff));
    }

private:
    TscTimestamp start;
    ProfilingStats& stats;
};

}  // namespace utils