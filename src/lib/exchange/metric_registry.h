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
    utils::Histogram add_order_latency;
    utils::Histogram cancel_order_latency;

    MetricRegistry()
        : add_order("orders_added_total", "Total number of orders added"),
          cancel_order("orders_cancelled_total", "Total number of orders cancelled"),
          add_order_latency("order_add_latency_seconds", "Order add latency in seconds",
                            {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1}),
          cancel_order_latency("order_cancel_latency_seconds", "Order cancel latency in seconds",
                               {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1}) {}

    /// Serialises all registered metrics into @p fmt.
    void render(utils::PrometheusFormatter& fmt) const {
        fmt.addCounter(add_order);
        fmt.addCounter(cancel_order);
        fmt.addHistogram(add_order_latency);
        fmt.addHistogram(cancel_order_latency);
    }
};

}  // namespace exchange