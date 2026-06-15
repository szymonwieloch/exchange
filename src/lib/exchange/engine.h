#pragma once

#include "book.h"
#include "definitions.h"
#include "lib/utils/log.h"
#include "lib/utils/profiler.h"
#include "lib/utils/thread.h"
#include "md.h"
#include "metric_registry.h"
#include "request.h"

namespace exchange {
class MatchingEngine final {
public:
    MatchingEngine(RequestLFQueue *user_requests, ResponseLFQueue *user_responses,
                   MDLFQueue *market_updates, MetricRegistry &metrics, utils::Logger &logger);
    ~MatchingEngine();
    void start();
    void stop();
    void processClientRequest(const Request *client_request) noexcept;

    // Deleted default, copy & move constructors and
    // assignment-operators.
    MatchingEngine() = delete;
    MatchingEngine(const MatchingEngine &) = delete;
    MatchingEngine(MatchingEngine &&) = delete;
    MatchingEngine &operator=(MatchingEngine &&) = delete;
    MatchingEngine &operator=(const MatchingEngine &&) = delete;

private:
    void run(const std::atomic<bool> &is_running) noexcept;

    RequestLFQueue *incoming_requests = nullptr;
    ResponseLFQueue *outgoing_ogw_responses = nullptr;
    MDLFQueue *outgoing_md_updates = nullptr;
    std::string time_str;
    utils::Logger &logger;
    utils::Thread engine_thread;
    MetricRegistry &metrics;
    OrderBookHashMap ticker_order_book;
};

}  // namespace exchange