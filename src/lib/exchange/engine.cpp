#include "engine.h"

#include "lib/utils/die.h"

namespace exchange {
MatchingEngine::MatchingEngine(RequestLFQueue *client_requests, ResponseLFQueue *client_responses,
                               MDLFQueue *market_updates, MetricRegistry &metrics,
                               utils::Logger &logger)
    : incoming_requests(client_requests),
      outgoing_ogw_responses(client_responses),
      outgoing_md_updates(market_updates),
      logger(logger),
      engine_thread("MatchingEngine", logger),
      metrics(metrics),
      ticker_order_book(&logger, outgoing_ogw_responses, outgoing_md_updates, metrics) {
    logger.info("MatchingEngine constructed");
}

MatchingEngine::~MatchingEngine() {
    engine_thread.stop();
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
    [[maybe_unused]] const bool started =
        engine_thread.start([this](const std::atomic<bool> &is_running) { run(is_running); });
    // TODO: handle start failure (already-running case)
}

void MatchingEngine::stop() {
    logger.info("MatchingEngine stopping");
    engine_thread.stop();
}

void MatchingEngine::run(const std::atomic<bool> &is_running) noexcept {
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
            utils::Profiler<int> profiler(metrics.add_order_latency);
            logger.debug(
                "NEW order: user=", static_cast<uint64_t>(type_safe::get(client_request->user_id)),
                " ticker=", static_cast<uint64_t>(type_safe::get(client_request->ticker_id)),
                " oid=", type_safe::get(client_request->order_id),
                " side=", static_cast<int32_t>(client_request->side),
                " price=", type_safe::get(client_request->price),
                " qty=", type_safe::get(client_request->qty));
            (void)order_book.add(client_request->user_id, client_request->order_id,
                                 client_request->ticker_id, client_request->side,
                                 client_request->price, client_request->qty);
            metrics.add_order.inc();

        } break;
        case RequestType::CANCEL: {
            utils::Profiler<int> profiler(metrics.cancel_order_latency);
            logger.debug("CANCEL request: user=",
                         static_cast<uint64_t>(type_safe::get(client_request->user_id)), " ticker=",
                         static_cast<uint64_t>(type_safe::get(client_request->ticker_id)),
                         " oid=", type_safe::get(client_request->order_id));
            order_book.cancel(client_request->user_id, client_request->order_id,
                              client_request->ticker_id);
            metrics.cancel_order.inc();
        } break;
        default: {
            logger.error("Received invalid request-type: ",
                         static_cast<uint32_t>(client_request->type));
            utils::die("Received invalid request-type");
        } break;
    }
}

}  // namespace exchange