#pragma once

namespace exchange {
enum struct Error {
    OK = 0,
    SELF_TRADE,
    TOO_MANY_ACTIVE_USERS,
    TOO_MANY_ORDERS,
    TOO_MANY_PRICE_LEVELS
};

bool operator!(Error err) noexcept {
    return err != Error::OK;
}

}  // namespace exchange