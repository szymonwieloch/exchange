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

    static MDUpdate add(TickerId ticker_id, Side side, Price price, Quantity qty,
                        Priority priority) noexcept {
        return MDUpdate{.type_ = MDUpdateType::ADD,
                        .order_id = OrderId::INVALID,
                        .ticker_id = ticker_id,
                        .side = side,
                        .price = price,
                        .qty = qty,
                        .priority = priority};
    }

    static MDUpdate modify(OrderId order_id, TickerId ticker_id, Side side, Price price,
                           Quantity qty, Priority priority) noexcept {
        return MDUpdate{.type_ = MDUpdateType::MODIFY,
                        .order_id = order_id,
                        .ticker_id = ticker_id,
                        .side = side,
                        .price = price,
                        .qty = qty,
                        .priority = priority};
    }

    static MDUpdate cancel(OrderId order_id, TickerId ticker_id, Side side, Price price,
                           Quantity qty) noexcept {
        return MDUpdate{.type_ = MDUpdateType::CANCEL,
                        .order_id = order_id,
                        .ticker_id = ticker_id,
                        .side = side,
                        .price = price,
                        .qty = qty};
    }

    static MDUpdate trade(TickerId ticker_id, Side side, Price price, Quantity qty) noexcept {
        return MDUpdate{.type_ = MDUpdateType::TRADE,
                        .ticker_id = ticker_id,
                        .side = side,
                        .price = price,
                        .qty = qty};
    }
};

using MDLFQueue = utils::LFQueue<MDUpdate>;

}  // namespace exchange