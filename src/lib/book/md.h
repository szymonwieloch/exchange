#pragma once

namespace book {

enum class MDUpdateType : uint8_t { INVALID = 0, ADD = 1, MODIFY = 2, CANCEL = 3, TRADE = 4 };

struct MDUpdate {
    MDUpdateType type_ = MDUpdateType::INVALID;
    OrderId order_id = OrderId_INVALID;
    TickerId ticker_id = TickerId_INVALID;
    Side side = Side::INVALID;
    Price price = Price_INVALID;
    Quantity qty = Quantity_INVALID;
    Priority priority = Priority_INVALID;
};

using MDLFQueue = utils::LFQueue<MDUpdate>;

}  // namespace book