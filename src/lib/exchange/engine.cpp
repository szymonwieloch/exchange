#include "engine.h"

#include "lib/utils/die.h"

namespace exchange {
MatchingEngine::MatchingEngine(RequestLFQueue *client_requests, ResponseLFQueue *client_responses,
                               MDLFQueue *market_updates)
    : incoming_requests(client_requests),
      outgoing_ogw_responses(client_responses),
      outgoing_md_updates(market_updates),
      logger("exchange_matching_engine.log", utils::LogLevel::INFO),
      ticker_order_book(&logger, outgoing_ogw_responses, outgoing_md_updates) {
    logger.info("MatchingEngine constructed");
}

MatchingEngine::~MatchingEngine() {
    is_running = false;
    logger.info("stopping MatchingEngine");
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(1s);
    incoming_requests = nullptr;
    outgoing_ogw_responses = nullptr;
    outgoing_md_updates = nullptr;
    logger.info("MatchingEngine destroyed");
}

void MatchingEngine::start() {
    logger.info("MatchingEngine starting");
    is_running = true;
    // TODO
    // ASSERT(
    //     Common::createAndStartThread(-1, "Exchange/MatchingEngine", [this]() { run(); }) !=
    //     nullptr, "Failed to start MatchingEngine thread.");
}

void MatchingEngine::stop() {
    logger.info("MatchingEngine stopping");
    is_running = false;
}

void MatchingEngine::run() noexcept {
    logger.info("starting MatchingEngine");
    while (is_running) {
        const auto req = incoming_requests->getNextToRead();
        if (req) [[likely]] {
            logger.debug("Processing order price=", type_safe::get(req->price),
                         " qty=", type_safe::get(req->qty), "  side=", (uint64_t)req->side);
            processClientRequest(req);
            incoming_requests->updateReadIndex();
        }
    }
}

void MatchingEngine::processClientRequest(const Request *client_request) noexcept {
    auto &order_book = *ticker_order_book.find(client_request->ticker_id);
    switch (client_request->type) {
        case RequestType::NEW: {
            logger.debug("NEW order: user=", static_cast<uint64_t>(type_safe::get(client_request->user_id)),
                         " ticker=", static_cast<uint64_t>(type_safe::get(client_request->ticker_id)),
                         " oid=", type_safe::get(client_request->order_id),
                         " side=", static_cast<int32_t>(client_request->side),
                         " price=", type_safe::get(client_request->price),
                         " qty=", type_safe::get(client_request->qty));
            (void)order_book.add(client_request->user_id, client_request->order_id,
                                 client_request->ticker_id, client_request->side,
                                 client_request->price, client_request->qty);

        } break;
        case RequestType::CANCEL: {
            logger.debug("CANCEL request: user=", static_cast<uint64_t>(type_safe::get(client_request->user_id)),
                         " ticker=", static_cast<uint64_t>(type_safe::get(client_request->ticker_id)),
                         " oid=", type_safe::get(client_request->order_id));
            order_book.cancel(client_request->user_id, client_request->order_id,
                              client_request->ticker_id);
        } break;
        default: {
            logger.error("Received invalid request-type: ", static_cast<uint32_t>(client_request->type));
            utils::die("Received invalid request-type");
        } break;
    }
}

}  // namespace exchange