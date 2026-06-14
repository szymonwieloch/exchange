#pragma once

#include "lib/utils.metrics.h"

namespace exchange {

struct ExchangeMetrics {
    utils::Counter add_order;
    utils::Counter cancel_order;
    utils::Histogram add_order_latency;
    utils::Histogram cancel_order_latency;
};

}  // namespace exchange