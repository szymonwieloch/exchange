#include "book.h"

#include "md.h"
#include "metric_registry.h"
#include "request.h"

namespace exchange {
OrderBook::OrderBook(TickerId ticker_id, utils::Logger* logger, ResponseLFQueue* responses,
                     MDLFQueue* market_updates, MetricRegistry& metrics)
    : ticker_id(ticker_id),
      responses(responses),
      market_updates(market_updates),
      order_pool(MAX_ORDER_IDS),
      logger(logger),
      metrics(&metrics) {
    logger->debug("OrderBook created for ticker=",
                  static_cast<uint64_t>(type_safe::get(ticker_id)));
}

OrderBook::~OrderBook() {
    logger->debug("OrderBook destroyed for ticker=",
                  static_cast<uint64_t>(type_safe::get(ticker_id)));
}

bool OrderBook::add(UserId user_id, OrderId order_id, TickerId ticker_id, Side side, Price price,
                    Quantity qty) noexcept {
    const auto new_market_order_id = generateNewMarketOrderId();
    auto response =
        Response::accepted(user_id, ticker_id, order_id, new_market_order_id, side, price, qty);
    sendResponse(response);

    logger->debug("Order accepted: user=", static_cast<uint64_t>(type_safe::get(user_id)),
                  " oid=", type_safe::get(order_id),
                  " market_oid=", type_safe::get(new_market_order_id),
                  " side=", static_cast<int32_t>(side), " price=", type_safe::get(price),
                  " qty=", type_safe::get(qty));

    const auto leaves_qty =
        checkForMatch(user_id, order_id, ticker_id, side, price, qty, new_market_order_id);

    if (leaves_qty != Quantity{0}) [[likely]] {
        const auto [priority, at_price_hint] = orders_at_price.nextPriority(price);
        auto order = order_pool.allocate(ticker_id, user_id, order_id, new_market_order_id, side,
                                         price, leaves_qty, priority);
        if (!addOrder(order, at_price_hint)) [[unlikely]] {
            order_pool.deallocate(order);
            logger->warn("Failed to add order to book (duplicate): user=",
                         static_cast<uint64_t>(type_safe::get(user_id)),
                         " oid=", type_safe::get(order_id));
            return false;
        }
        auto market_update = MDUpdate::add(ticker_id, side, price, leaves_qty, priority);
        sendMarketUpdate(market_update);

        logger->debug("Order inserted into book: market_oid=", type_safe::get(new_market_order_id),
                      " leaves_qty=", type_safe::get(leaves_qty),
                      " priority=", type_safe::get(priority));
    } else {
        logger->debug("Order fully matched: market_oid=", type_safe::get(new_market_order_id));
    }
    return true;
}

void OrderBook::cancel(UserId user_id, OrderId order_id, TickerId ticker_id) noexcept {
    logger->debug("Cancel request: user=", static_cast<uint64_t>(type_safe::get(user_id)),
                  " oid=", type_safe::get(order_id),
                  " ticker=", static_cast<uint64_t>(type_safe::get(ticker_id)));

    auto order = cid_oid_to_order.find(user_id, order_id);
    if (!order) [[unlikely]] {
        logger->debug("Cancel rejected (order not found): user=",
                      static_cast<uint64_t>(type_safe::get(user_id)),
                      " oid=", type_safe::get(order_id));
        Response response = Response::cancelRejected(user_id, ticker_id, order_id);
        sendResponse(response);
        return;
    }

    logger->debug("Order canceled: user=", static_cast<uint64_t>(type_safe::get(user_id)),
                  " oid=", type_safe::get(order_id),
                  " market_oid=", type_safe::get(order->market_order_id),
                  " side=", static_cast<int32_t>(order->side),
                  " price=", type_safe::get(order->price), " qty=", type_safe::get(order->qty));

    auto response = Response::canceled(user_id, ticker_id, order_id, order->market_order_id,
                                       order->side, order->price, order->qty);
    auto market_update =
        MDUpdate::cancel(order->market_order_id, ticker_id, order->side, order->price, order->qty);

    removeOrder(order);  // can remove only when the messages are created

    sendResponse(response);
    sendMarketUpdate(market_update);
}

Quantity OrderBook::checkForMatch(UserId user_id, OrderId client_order_id, TickerId ticker_id,
                                  Side side, Price price, Quantity qty,
                                  MarketOrderId new_market_order_id) noexcept {
    logger->debug("Matching started: market_oid=", type_safe::get(new_market_order_id),
                  " side=", static_cast<int32_t>(side), " price=", type_safe::get(price),
                  " qty=", type_safe::get(qty));

    auto leaves_qty = qty;

    // The matching loop is identical for both sides except for:
    //  - which list head to use (bids / asks),
    //  - whether the price check is < or >.
    // Extract it into a lambda to keep the hot path DRY without virtual dispatch.
    //
    // The head pointer is re-derived from the side-head each iteration rather
    // than carried across match() calls, because match() can destroy the entire
    // price level (via removeOrdersAtPrice), which updates bids_by_price /
    // asks_by_price to the next level and deallocates the old head.  Using a
    // stale copy would be a use-after-free.
    auto match_side = [&](auto get_head, auto price_cond) {
        while (leaves_qty != Quantity{0}) {
            auto* head = get_head();
            if (!head)
                return;
            auto* itr = head->first_order;
            if (price_cond(price, itr->price)) [[likely]] {
                return;  // no more matchable prices on this side
            }
            match(ticker_id, user_id, side, client_order_id, new_market_order_id, head, itr,
                  &leaves_qty);
        }
    };

    if (side == Side::BUY) {
        match_side([this] { return orders_at_price.asks(); },
                   [](Price a, Price b) noexcept { return a < b; });
    } else {
        match_side([this] { return orders_at_price.bids(); },
                   [](Price a, Price b) noexcept { return a > b; });
    }

    logger->debug("Matching complete: market_oid=", type_safe::get(new_market_order_id),
                  " leaves_qty=", type_safe::get(leaves_qty), " filled_qty=",
                  type_safe::get(Quantity{type_safe::get(qty) - type_safe::get(leaves_qty)}));

    return leaves_qty;
}

void OrderBook::match(TickerId ticker_id, UserId user_id, Side side, OrderId client_order_id,
                      MarketOrderId new_market_order_id, OrdersAtPrice* price_level, Order* itr,
                      Quantity* leaves_qty) noexcept {
    const auto order = itr;
    const auto order_qty = order->qty;
    const auto fill_qty = std::min(*leaves_qty, order_qty);
    *leaves_qty -= fill_qty;
    order->qty -= fill_qty;

    logger->debug("Fill: aggressor_market_oid=", type_safe::get(new_market_order_id),
                  " resting_market_oid=", type_safe::get(order->market_order_id),
                  " price=", type_safe::get(itr->price), " fill_qty=", type_safe::get(fill_qty),
                  " resting_remaining=", type_safe::get(order->qty));

    sendResponse(Response::filled(user_id, ticker_id, client_order_id, new_market_order_id, side,
                                  itr->price, fill_qty, *leaves_qty));

    sendResponse(Response::filled(order->user_id, ticker_id, order->order_id,
                                  order->market_order_id, order->side, itr->price, fill_qty,
                                  order->qty));

    metrics->match_order.inc();

    auto market_update = MDUpdate::trade(ticker_id, side, itr->price, fill_qty);
    sendMarketUpdate(market_update);

    if (order->qty == Quantity{0}) {
        auto market_update = MDUpdate::cancel(order->market_order_id, ticker_id, order->side,
                                              order->price, order_qty);
        sendMarketUpdate(market_update);
        removeOrder(order, price_level);
    } else {
        auto market_update = MDUpdate::modify(order->market_order_id, ticker_id, order->side,
                                              order->price, order->qty, order->priority);
        sendMarketUpdate(market_update);
    }
}

bool OrderBook::addOrder(Order* order, OrdersAtPrice* at_price_hint) noexcept {
    if (!cid_oid_to_order.insert(order)) [[unlikely]] {
        logger->debug("addOrder failed: duplicate user-oid pair user=",
                      static_cast<uint64_t>(type_safe::get(order->user_id)),
                      " oid=", type_safe::get(order->order_id));
        return false;
    }
    if (!orders_at_price.insert(order, at_price_hint)) [[unlikely]] {
        cid_oid_to_order.remove(order->user_id, order->order_id);
        logger->debug("addOrder failed: orders_at_price insert failed price=",
                      type_safe::get(order->price));
        return false;
    }
    metrics->active_orders.inc();
    return true;
}

void OrderBook::removeOrder(Order* order, OrdersAtPrice* at_price_hint) noexcept {
    logger->debug("Removing order: market_oid=", type_safe::get(order->market_order_id),
                  " user=", static_cast<uint64_t>(type_safe::get(order->user_id)),
                  " oid=", type_safe::get(order->order_id),
                  " price=", type_safe::get(order->price));

    cid_oid_to_order.remove(order->user_id, order->order_id);
    if (at_price_hint) {
        orders_at_price.remove(order, at_price_hint);
    } else {
        orders_at_price.remove(order);
    }
    order_pool.deallocate(order);
    metrics->active_orders.dec();
}

void OrderBook::sendResponse(const Response& response) noexcept {
    *responses->getNextToWriteTo() = response;
    responses->updateWriteIndex();
}

void OrderBook::sendMarketUpdate(const MDUpdate& market_update) noexcept {
    *market_updates->getNextToWriteTo() = market_update;
    market_updates->updateWriteIndex();
}

}  // namespace exchange