#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "lib/utils/metrics.h"
#include "lib/utils/metrics_server.h"

using namespace utils;

// ===================================================================
//  Counter tests
// ===================================================================

TEST(CounterTest, StartsAtZero) {
    Counter c("test_counter");
    EXPECT_EQ(c.value(), 0ULL);
}

TEST(CounterTest, IncrementsByOne) {
    Counter c("test_counter");
    c.inc();
    EXPECT_EQ(c.value(), 1ULL);
}

TEST(CounterTest, IncrementsByDelta) {
    Counter c("test_counter");
    c.inc(5);
    c.inc(3);
    EXPECT_EQ(c.value(), 8ULL);
}

TEST(CounterTest, NameAndHelp) {
    Counter c("orders_total", "Total orders processed.");
    EXPECT_EQ(c.name(), "orders_total");
    EXPECT_EQ(c.help(), "Total orders processed.");
}

TEST(CounterTest, ThreadSafe) {
    Counter c("concurrent_counter");
    constexpr int kThreads = 4;
    constexpr int kIncPerThread = 10000;
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&c]() {
            for (int j = 0; j < kIncPerThread; ++j) {
                c.inc();
            }
        });
    }
    for (auto& t : threads)
        t.join();
    EXPECT_EQ(c.value(), static_cast<uint64_t>(kThreads * kIncPerThread));
}

// ===================================================================
//  Gauge tests
// ===================================================================

TEST(GaugeTest, StartsAtZero) {
    Gauge g("test_gauge");
    EXPECT_EQ(g.value(), 0);
}

TEST(GaugeTest, SetAbsolute) {
    Gauge g("test_gauge");
    g.set(42);
    EXPECT_EQ(g.value(), 42);
}

TEST(GaugeTest, IncDec) {
    Gauge g("test_gauge");
    g.inc(10);
    EXPECT_EQ(g.value(), 10);
    g.dec(3);
    EXPECT_EQ(g.value(), 7);
}

TEST(GaugeTest, GoesNegative) {
    Gauge g("test_gauge");
    g.dec(5);
    EXPECT_EQ(g.value(), -5);
}

TEST(GaugeTest, ThreadSafe) {
    Gauge g("concurrent_gauge");
    constexpr int kThreads = 4;
    constexpr int kOpsPerThread = 10000;
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&g]() {
            for (int j = 0; j < kOpsPerThread; ++j) {
                g.inc();
                g.dec();
            }
        });
    }
    for (auto& t : threads)
        t.join();
    EXPECT_EQ(g.value(), 0);
}

// ===================================================================
//  Histogram tests
// ===================================================================

TEST(HistogramTest, StartsAtZero) {
    Histogram h("test_histogram", "", {1.0, 5.0, 10.0});
    EXPECT_EQ(h.count(), 0ULL);
    EXPECT_DOUBLE_EQ(h.sum(), 0.0);
    EXPECT_EQ(h.bucketCount(), 3);
}

TEST(HistogramTest, SingleObservation) {
    Histogram h("test_histogram", "", {1.0, 5.0, 10.0});
    h.observe(3.0);
    EXPECT_EQ(h.count(), 1ULL);
    EXPECT_DOUBLE_EQ(h.sum(), 3.0);

    auto buckets = h.bucketValues();
    // 3.0 <= 1.0? No.  3.0 <= 5.0? Yes.  3.0 <= 10.0? Yes.  +Inf: count
    EXPECT_EQ(buckets[0], 0ULL);
    EXPECT_EQ(buckets[1], 1ULL);
    EXPECT_EQ(buckets[2], 1ULL);
    EXPECT_EQ(buckets[3], 1ULL);  // +Inf
}

TEST(HistogramTest, MultipleObservations) {
    Histogram h("test_histogram", "", {0.5, 1.0, 2.0});
    h.observe(0.3);  // in bucket 0,1,2
    h.observe(0.7);  // in bucket 1,2
    h.observe(1.5);  // in bucket 2
    h.observe(3.0);  // only +Inf

    EXPECT_EQ(h.count(), 4ULL);
    EXPECT_DOUBLE_EQ(h.sum(), 0.3 + 0.7 + 1.5 + 3.0);

    auto buckets = h.bucketValues();
    EXPECT_EQ(buckets[0], 1ULL);  // one value <= 0.5
    EXPECT_EQ(buckets[1], 2ULL);  // two values <= 1.0
    EXPECT_EQ(buckets[2], 3ULL);  // three values <= 2.0
    EXPECT_EQ(buckets[3], 4ULL);  // all four in +Inf
}

TEST(HistogramTest, BucketBounds) {
    Histogram h("test_histogram", "", {0.01, 0.05, 0.1});
    EXPECT_DOUBLE_EQ(h.bucketBound(0), 0.01);
    EXPECT_DOUBLE_EQ(h.bucketBound(1), 0.05);
    EXPECT_DOUBLE_EQ(h.bucketBound(2), 0.1);
    EXPECT_TRUE(std::isinf(h.bucketBound(3)));
}

// ===================================================================
//  MetricsRegistry tests — skipped (MetricsRegistry not yet implemented)
// ===================================================================
#if 0
TEST(MetricsRegistryTest, EmptyCollect) {
    MetricsRegistry reg;
    std::string result = reg.collect();
    EXPECT_FALSE(result.empty());
}

TEST(MetricsRegistryTest, CollectsCounters) {
    MetricsRegistry reg;
    auto* c = reg.add(std::make_unique<Counter>("my_counter", "A counter."));
    c->inc(5);

    std::string result = reg.collect();
    EXPECT_NE(result.find("# HELP my_counter A counter."), std::string::npos);
    EXPECT_NE(result.find("# TYPE my_counter counter"), std::string::npos);
    EXPECT_NE(result.find("my_counter 5"), std::string::npos);
}

TEST(MetricsRegistryTest, CollectsGauges) {
    MetricsRegistry reg;
    auto* g = reg.add(std::make_unique<Gauge>("my_gauge", "A gauge."));
    g->set(-3);

    std::string result = reg.collect();
    EXPECT_NE(result.find("# HELP my_gauge A gauge."), std::string::npos);
    EXPECT_NE(result.find("# TYPE my_gauge gauge"), std::string::npos);
    EXPECT_NE(result.find("my_gauge -3"), std::string::npos);
}

TEST(MetricsRegistryTest, CollectsHistograms) {
    MetricsRegistry reg;
    auto* h = reg.add(std::make_unique<Histogram>("my_hist", "A histogram.", std::vector<double>{0.5, 1.0}));
    h->observe(0.3);
    h->observe(0.7);

    std::string result = reg.collect();
    EXPECT_NE(result.find("# HELP my_hist A histogram."), std::string::npos);
    EXPECT_NE(result.find("# TYPE my_hist histogram"), std::string::npos);
    EXPECT_NE(result.find("my_hist_bucket{le=\"0.5\"} 1"), std::string::npos);
    EXPECT_NE(result.find("my_hist_bucket{le=\"1\"} 2"), std::string::npos);
    EXPECT_NE(result.find("my_hist_bucket{le=\"+Inf\"} 2"), std::string::npos);
    EXPECT_NE(result.find("my_hist_sum "), std::string::npos);
    EXPECT_NE(result.find("my_hist_count 2"), std::string::npos);
}
#endif

// ===================================================================
//  MetricsServer integration test
// ===================================================================

TEST(MetricsServerTest, StartStop) {
    auto cb = [](PrometheusFormatter&) { };
    MetricsServer::Config cfg{.bind_address = "127.0.0.1", .port = 19090};
    MetricsServer server(cb, cfg);

    EXPECT_TRUE(server.start());
    EXPECT_TRUE(server.isRunning());
    EXPECT_EQ(server.port(), 19090);

    server.stop();
    EXPECT_FALSE(server.isRunning());
}

TEST(MetricsServerTest, DoubleStartIsIdempotent) {
    auto cb = [](PrometheusFormatter&) { };
    MetricsServer::Config cfg{.bind_address = "127.0.0.1", .port = 19091};
    MetricsServer server(cb, cfg);

    EXPECT_TRUE(server.start());
    EXPECT_TRUE(server.start());  // should return true without error
    server.stop();
}

TEST(MetricsServerTest, StopWithoutStartIsSafe) {
    auto cb = [](PrometheusFormatter&) { };
    MetricsServer server(cb, MetricsServer::Config{});
    server.stop();  // should not crash
}
