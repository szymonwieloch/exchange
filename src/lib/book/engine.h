#pragma once

#include "book.h"
#include "definitions.h"
#include "lib/utils/log.h"
#include "md.h"
#include "request.h"

namespace book {
class MatchingEngine final {
public:
    MatchingEngine(RequestLFQueue *user_requests, ResponseLFQueue *user_responses,
                   MDLFQueue *market_updates);
    ~MatchingEngine();
    auto start() -> void;
    auto stop() -> void;

    // Deleted default, copy & move constructors and
    // assignment-operators.
    MatchingEngine() = delete;
    MatchingEngine(const MatchingEngine &) = delete;
    MatchingEngine(const MatchingEngine &&) = delete;
    MatchingEngine &operator=(const MatchingEngine &) = delete;
    MatchingEngine &operator=(const MatchingEngine &&) = delete;

private:
    OrderMap ticker_order_book;
    RequestLFQueue *incoming_requests = nullptr;
    ResponseLFQueue *outgoing_ogw_responses = nullptr;
    MDLFQueue *outgoing_md_updates = nullptr;
    volatile bool run = false;
    std::string time_str;
    utils::Logger logger;
};

}  // namespace book