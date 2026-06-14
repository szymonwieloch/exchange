#pragma once

#include <iostream>
#include <memory>
#include <string>

#include "lib/main/config.h"
#include "lib/utils/metrics.h"
#include "lib/utils/metrics_server.h"

namespace exchange {

/// Registry holding all Prometheus metrics for the exchange.
///
/// Owns the metric objects and provides a render() method that
/// serialises them into the supplied PrometheusFormatter on each
/// /metrics scrape.
class MetricRegistry final {
public:
    utils::Counter add_order;
    utils::Counter cancel_order;
    utils::Histogram<std::chrono::nanoseconds> add_order_latency;
    utils::Histogram<std::chrono::nanoseconds> cancel_order_latency;

    MetricRegistry()
        : add_order("orders_added_total", "Total number of orders added"),
          cancel_order("orders_cancelled_total", "Total number of orders cancelled"),
          add_order_latency("order_add_latency_seconds", "Order add latency in seconds",
                            {std::chrono::nanoseconds(100'000),
                             std::chrono::nanoseconds(500'000),
                             std::chrono::nanoseconds(1'000'000),
                             std::chrono::nanoseconds(5'000'000),
                             std::chrono::nanoseconds(10'000'000),
                             std::chrono::nanoseconds(50'000'000),
                             std::chrono::nanoseconds(100'000'000)}),
          cancel_order_latency("order_cancel_latency_seconds", "Order cancel latency in seconds",
                               {std::chrono::nanoseconds(100'000),
                                std::chrono::nanoseconds(500'000),
                                std::chrono::nanoseconds(1'000'000),
                                std::chrono::nanoseconds(5'000'000),
                                std::chrono::nanoseconds(10'000'000),
                                std::chrono::nanoseconds(50'000'000),
                                std::chrono::nanoseconds(100'000'000)}) {}

    /// Serialises all registered metrics into @p fmt.
    void render(utils::PrometheusFormatter& fmt) const {
        fmt.addCounter(add_order);
        fmt.addCounter(cancel_order);
        fmt.addHistogram(add_order_latency);
        fmt.addHistogram(cancel_order_latency);
    }
};

}  // namespace exchange