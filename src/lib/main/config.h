#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

#include "lib/utils/log.h"

namespace exchange {

/// Configuration parsed from a TOML file.
///
/// All settings are populated at startup by @ref parseConfig. The struct is
/// designed to be passed to component constructors so that every subsystem can
/// extract the settings it needs without coupling to TOML.
///
/// Example TOML:
/// ```
/// [logging]
/// level = "info"
/// file  = "exchange.log"
///
/// [engine]
/// tickers = ["AAPL", "GOOG", "MSFT"]
///
/// [threading]
/// engine_core = 0
/// logger_core = 1
/// ```
struct Config {
    /// Logging subsystem configuration.
    struct Logging {
        std::string file = "exchange.log";
        utils::LogLevel level = utils::LogLevel::INFO;
    };

    /// Matching-engine configuration.
    struct Engine {
        std::vector<std::string> tickers;
    };

    /// Thread-pinning configuration.
    struct Threading {
        int engine_core = -1;  ///< -1 = no pinning
        int logger_core = -1;  ///< -1 = no pinning
    };

    Logging logging;
    Engine engine;
    Threading threading;
};

/// Parses a LogLevel from a TOML string.
///
/// Recognised values (case-insensitive): "debug", "info", "warn", "error".
/// Returns empty option on unknown value.
[[nodiscard]] inline std::optional<utils::LogLevel> parseLogLevel(std::string_view level) noexcept {
    if (level == "debug")
        return utils::LogLevel::DEBUG;
    else if (level == "info")
        return utils::LogLevel::INFO;
    else if (level == "warn")
        return utils::LogLevel::WARN;
    else if (level == "error")
        return utils::LogLevel::ERROR;
    return std::nullopt;
}

/// Parses the [logging] section of a TOML table.
[[nodiscard]] inline std::expected<Config::Logging, std::string> parseLogging(
    const toml::table& tbl) noexcept {
    Config::Logging result;

    if (const auto* logging = tbl["logging"].as_table()) {
        if (auto val = (*logging)["level"].value<std::string>()) {
            auto level = parseLogLevel(*val);
            if (level) {
                result.level = *level;
            } else {
                return std::unexpected(std::string{"Unknown log level: "} + *val);
            }
        }
        if (auto val = (*logging)["file"].value<std::string>()) {
            result.file = *val;
        }
    }

    return result;
}

/// Parses the [engine] section of a TOML table.
[[nodiscard]] inline std::expected<Config::Engine, std::string> parseEngine(
    const toml::table& tbl) noexcept {
    Config::Engine result;

    if (const auto* engine = tbl["engine"].as_table()) {
        if (auto* arr = (*engine)["tickers"].as_array()) {
            for (const auto& item : *arr) {
                if (auto val = item.value<std::string>()) {
                    result.tickers.push_back(*val);
                }
            }
        }
    }

    return result;
}

/// Parses the [threading] section of a TOML table.
[[nodiscard]] inline std::expected<Config::Threading, std::string> parseThreading(
    const toml::table& tbl) noexcept {
    Config::Threading result;

    if (const auto* threading = tbl["threading"].as_table()) {
        if (auto val = (*threading)["engine_core"].value<int64_t>()) {
            result.engine_core = static_cast<int>(*val);
        }
        if (auto val = (*threading)["logger_core"].value<int64_t>()) {
            result.logger_core = static_cast<int>(*val);
        }
    }

    return result;
}

/// Parses a TOML configuration file into a @ref Config struct.
///
/// @param path  Path to the TOML file.
/// @return  Config on success; error string on parse/validation failure.
[[nodiscard]] inline std::expected<Config, std::string> parseConfig(
    const std::string& path) noexcept {
    try {
        const auto tbl = toml::parse_file(path);

        auto logging = parseLogging(tbl);
        if (!logging)
            return std::unexpected(logging.error());

        auto engine = parseEngine(tbl);
        if (!engine)
            return std::unexpected(engine.error());

        auto threading = parseThreading(tbl);
        if (!threading)
            return std::unexpected(threading.error());

        return Config{
            std::move(*logging),
            std::move(*engine),
            std::move(*threading),
        };
    } catch (const toml::parse_error& err) {
        return std::unexpected(std::string{err.what()});
    }
}

}  // namespace exchange
