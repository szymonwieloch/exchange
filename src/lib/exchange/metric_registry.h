#pragma once

#include <iostream>
#include <memory>
#include <string>

#include "lib/main/config.h"
#include "lib/utils/metrics.h"
#include "lib/utils/metrics_server.h"

namespace exchange {

struct ExchangeMetrics {
    utils::Counter add_order;
    utils::Counter cancel_order;
    utils::Histogram add_order_latency;
    utils::Histogram cancel_order_latency;

    ExchangeMetrics()
        : add_order("orders_added_total", "Total number of orders added"),
          cancel_order("orders_cancelled_total", "Total number of orders cancelled"),
          add_order_latency("order_add_latency_seconds", "Order add latency in seconds",
                            {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1}),
          cancel_order_latency("order_cancel_latency_seconds", "Order cancel latency in seconds",
                               {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1}) {}
};

/// Registry holding all Prometheus metrics for the exchange.
///
/// Owns the metric objects and provides a render() method that
/// serialises them into Prometheus text format on each /metrics scrape.
class MetricRegistry final {
public:
    ExchangeMetrics metrics;

    /// Renders all registered metrics in Prometheus text format.
    [[nodiscard]] std::string render() const {
        utils::PrometheusFormatter fmt;
        fmt.addCounter(metrics.add_order);
        fmt.addCounter(metrics.cancel_order);
        fmt.addHistogram(metrics.add_order_latency);
        fmt.addHistogram(metrics.cancel_order_latency);
        return fmt.complete();
    }
};

}  // namespace exchange