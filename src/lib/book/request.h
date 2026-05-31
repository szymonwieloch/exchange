#pragma once

#include "definitions.h"
#include "lib/utils/queue.h"

namespace book {

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
    UserId user_id = UserId_INVALID;
    TickerId ticker_id = TickerId_INVALID;
    OrderId order_id = OrderId_INVALID;
    Side side = Side::INVALID;
    Price price = Price_INVALID;
    Quantity qty = Quantity_INVALID;
};

struct Response {
    ResponseType type = ResponseType::INVALID;
    UserId user_id = UserId_INVALID;
    TickerId ticker_id = TickerId_INVALID;
    OrderId order_id = OrderId_INVALID;
    OrderId market_order_id = OrderId_INVALID;
    Side side = Side::INVALID;
    Price price = Price_INVALID;
    Quantity exec_qty = Quantity_INVALID;
    Quantity leaves_qty = Quantity_INVALID;

    static Response accepted(UserId user_id, TickerId ticker_id, OrderId order_id,
                             OrderId market_order_id, Side side, Price price,
                             Quantity qty) noexcept {
        return Response{.type = ResponseType::ACCEPTED,
                        .user_id = user_id,
                        .ticker_id = ticker_id,
                        .order_id = order_id,
                        .market_order_id = market_order_id,
                        .side = side,
                        .price = price,
                        .exec_qty = 0,
                        .leaves_qty = qty};
    }

    static Response canceled(UserId user_id, TickerId ticker_id, OrderId order_id,
                             OrderId market_order_id, Side side, Price price,
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

    static Response cancelRejected(UserId user_id, TickerId ticker_id, OrderId order_id) {
        return Response{
            .type = ResponseType::CANCEL_REJECTED,
            .user_id = user_id,
            .ticker_id = ticker_id,
            .order_id = order_id,
        };
    }
};

// TODO #pragma pack(pop)
using RequestLFQueue = utils::LFQueue<Request>;
using ResponseLFQueue = utils::LFQueue<Response>;

// #pragma pack(push, 1)

}  // namespace book