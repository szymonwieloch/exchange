#include "engine.h"

namespace book {
MatchingEngine::MatchingEngine(RequestLFQueue *client_requests, ResponseLFQueue *client_responses,
                               MDLFQueue *market_updates)
    : incoming_requests(client_requests),
      outgoing_ogw_responses(client_responses),
      outgoing_md_updates(market_updates),
      logger("exchange_matching_engine.log") {
    for (size_t i = 0; i < ticker_order_book.size(); ++i) {
        ticker_order_book[i] = new OrderBook(i, &logger, this);
    }
}

MatchingEngine::~MatchingEngine() {
    is_running = false;
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(1s);
    incoming_requests = nullptr;
    outgoing_ogw_responses = nullptr;
    outgoing_md_updates = nullptr;
    for (auto &order_book : ticker_order_book) {
        delete order_book;
        order_book = nullptr;
    }
}

void MatchingEngine::start() {
    is_running = true;
    // TODO
    // ASSERT(
    //     Common::createAndStartThread(-1, "Exchange/MatchingEngine", [this]() { run(); }) !=
    //     nullptr, "Failed to start MatchingEngine thread.");
}

void MatchingEngine::stop() {
    is_running = false;
}

void MatchingEngine::run() noexcept {
    // logger.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__,
    // utils::getCurrentTimeStr(&time_str));
    while (is_running) {
        const auto me_client_request = incoming_requests->getNextToRead();
        if (me_client_request) [[likely]] {
            // logger.log("%:% %() % Processing %\n", __FILE__, __LINE__, __FUNCTION__,
            // utils::getCurrentTimeStr(&time_str), me_client_request->toString());
            processClientRequest(me_client_request);
            incoming_requests->updateReadIndex();
        }
    }
}

void MatchingEngine::processClientRequest(const Request *client_request) noexcept {
    // auto order_book = ticker_order_book[client_request->ticker_id];
    switch (client_request->type) {
        case RequestType::NEW: {
            // order_book->add(client_request->user_id, client_request->order_id,
            //                 client_request->ticker_id, client_request->side,
            //                 client_request->price, client_request->qty);

        } break;
        case RequestType::CANCEL: {
            // order_book->cancel(client_request->user_id, client_request->order_id,
            //                    client_request->ticker_id);
        } break;
        default: {
            utils::die("Received invalid request-type");
        } break;
    }
}
}  // namespace book