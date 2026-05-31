#pragma once

namespace exchange {

enum class MDUpdateType : uint8_t { INVALID = 0, ADD = 1, MODIFY = 2, CANCEL = 3, TRADE = 4 };

struct MDUpdate {
    MDUpdateType type_ = MDUpdateType::INVALID;
    OrderId order_id = OrderId::INVALID;
    TickerId ticker_id = TickerId::INVALID;
    Side side = Side::INVALID;
    Price price = Price::INVALID;
    Quantity qty = Quantity::INVALID;
    Priority priority = Priority::INVALID;
};

using MDLFQueue = utils::LFQueue<MDUpdate>;

}  // namespace exchange