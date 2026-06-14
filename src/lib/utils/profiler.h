#pragma once

#include "time.h"

namespace utils {

template <typename MeasurementId, typename Handler>
class Profiler {
public:
    Profiler(MeasurementId mes_id) : start(readTsc()), mes_id(mes_id) {}
    ~Profiler() {
        TscDuration diff = readTsc() - start;
        Handler(mes_id, diff);
    }

private:
    TscTimestamp start;
    MeasurementId mes_id;
};

}  // namespace utils