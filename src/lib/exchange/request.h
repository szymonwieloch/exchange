#pragma once

#include "definitions.h"
#include "lib/utils/queue.h"

namespace exchange {

enum class RequestType : uint8_t { INVALID = 0, NEW = 1, CANCEL = 2 };

// #pragma pack(push, 1)
enum class ResponseType : uint8_t {
    INVALID = 0,
    ACCEPTED = 1,
    CANCELED = 2,
    FILLED = 3,
    CANCEL_REJECTED = 4
};

struct Request {
    RequestType type = RequestType::INVALID;
    SessionId session_id = SessionId::INVALID;
    UserId user_id = UserId::INVALID;
    TickerId ticker_id = TickerId::INVALID;
    OrderId order_id = OrderId::INVALID;
    Side side = Side::INVALID;
    Price price = Price::INVALID;
    Quantity qty = Quantity::INVALID;
};

struct Response {
    ResponseType type = ResponseType::INVALID;
    SessionId session_id = SessionId::INVALID;
    UserId user_id = UserId::INVALID;
    TickerId ticker_id = TickerId::INVALID;
    OrderId order_id = OrderId::INVALID;
    MarketOrderId market_order_id = MarketOrderId::INVALID;
    Side side = Side::INVALID;
    Price price = Price::INVALID;
    Quantity exec_qty = Quantity::INVALID;
    Quantity leaves_qty = Quantity::INVALID;

    static Response accepted(UserId user_id, TickerId ticker_id, OrderId order_id,
                             MarketOrderId market_order_id, Side side, Price price,
                             Quantity qty) noexcept {
        return Response{.type = ResponseType::ACCEPTED,
                        .user_id = user_id,
                        .ticker_id = ticker_id,
                        .order_id = order_id,
                        .market_order_id = market_order_id,
                        .side = side,
                        .price = price,
                        .exec_qty = Quantity{0},
                        .leaves_qty = qty};
    }

    static Response canceled(UserId user_id, TickerId ticker_id, OrderId order_id,
                             MarketOrderId market_order_id, Side side, Price price,
                             Quantity qty) noexcept {
        return Response{.type = ResponseType::CANCELED,
                        .user_id = user_id,
                        .ticker_id = ticker_id,
                        .order_id = order_id,
                        .market_order_id = market_order_id,
                        .side = side,
                        .price = price,
                        .leaves_qty = qty};
    }

    static Response cancelRejected(UserId user_id, TickerId ticker_id, OrderId order_id) noexcept {
        return Response{
            .type = ResponseType::CANCEL_REJECTED,
            .user_id = user_id,
            .ticker_id = ticker_id,
            .order_id = order_id,
        };
    }

    static Response filled(UserId user_id, TickerId ticker_id, OrderId order_id,
                           MarketOrderId market_order_id, Side side, Price price, Quantity exec_qty,
                           Quantity leaves_qty) noexcept {
        return Response{.type = ResponseType::FILLED,
                        .user_id = user_id,
                        .ticker_id = ticker_id,
                        .order_id = order_id,
                        .market_order_id = market_order_id,
                        .side = side,
                        .price = price,
                        .exec_qty = exec_qty,
                        .leaves_qty = leaves_qty};
    }
};

// TODO #pragma pack(pop)
using RequestLFQueue = utils::MPSCQueue<Request>;
using ResponseLFQueue = utils::SPSCQueue<Response>;

// #pragma pack(push, 1)

}  // namespace exchange