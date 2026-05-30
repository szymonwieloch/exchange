#pragma once

#include "definitions.h"

namespace book {

struct Order {
    OrderType type = OrderType::INVALID;
    UserId user_id = UserId_INVALID;
    TickerId ticker_id = TickerId_INVALID;
    OrderId order_id = OrderId_INVALID;
    Side side = Side::INVALID;
    Price price = Price_INVALID;
    Quantity qty = Quantity_INVALID;
};
}  // namespace book