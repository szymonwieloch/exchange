#pragma once

#include <charconv>
#include <string>

#include "definitions.h"

namespace exchange {

class UserManager {
public:
    std::optional<UserId> checkUser(std::string_view name,
                                    std::string_view password) const noexcept {
        // Fake implementation emulating user authentication.
        // Real implementation would use a database and password hashes or TLS-based auth.

        // Assumption: "UserX" / "PasswordX" where X is the user ID are valid credentials.
        if (!name.starts_with("User") || !password.starts_with("Password")) {
            return std::nullopt;
        }

        auto name_id_str = name.substr(4);
        auto pass_id_str = password.substr(8);

        std::uint32_t name_user_id = 0;
        std::uint32_t pass_user_id = 0;
        auto [name_ptr, name_ec] =
            std::from_chars(name_id_str.begin(), name_id_str.end(), name_user_id);
        auto [pass_ptr, pass_ec] =
            std::from_chars(pass_id_str.begin(), pass_id_str.end(), pass_user_id);

        if (name_ec != std::errc{} || pass_ec != std::errc{} || name_user_id != pass_user_id) {
            return std::nullopt;
        }

        return UserId{name_user_id};
    }
};

}  // namespace exchange