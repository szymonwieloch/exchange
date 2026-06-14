#pragma once

#include <cstdint>
#include <expected>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

#include "lib/utils/log.h"

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

    /// Prometheus metrics server configuration.
    struct Metrics {
        bool enabled = false;
        uint16_t port = 9090;
        std::string bind_address = "127.0.0.1";
    };

    Logging logging;
    Engine engine;
    Threading threading;
    Metrics metrics;
};

/// Validates that a TOML table contains only the allowed keys.
///
/// Returns std::nullopt on success, or an error string naming the first
/// unknown key found.
[[nodiscard]] inline std::optional<std::string> validateTableKeys(
    const toml::table& tbl, std::initializer_list<std::string_view> allowed) noexcept {
    for (const auto& [key, value] : tbl) {
        bool known = false;
        for (auto a : allowed) {
            if (key.str() == a) {
                known = true;
                break;
            }
        }
        if (!known) {
            return std::string{"Unknown key: "} + std::string{key.str()};
        }
    }
    return std::nullopt;
}

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

        if (auto err = validateTableKeys(*logging, {"level", "file"})) {
            return std::unexpected(std::move(*err));
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

        if (auto err = validateTableKeys(*engine, {"tickers"})) {
            return std::unexpected(std::move(*err));
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

        if (auto err = validateTableKeys(*threading, {"engine_core", "logger_core"})) {
            return std::unexpected(std::move(*err));
        }
    }

    return result;
}

/// Parses the [metrics] section of a TOML table.
[[nodiscard]] inline std::expected<Config::Metrics, std::string> parseMetrics(
    const toml::table& tbl) noexcept {
    Config::Metrics result;

    if (const auto* metrics = tbl["metrics"].as_table()) {
        if (auto val = (*metrics)["enabled"].value<bool>()) {
            result.enabled = *val;
        }
        if (auto val = (*metrics)["port"].value<int64_t>()) {
            if (*val < 1 || *val > 65535) {
                return std::unexpected("metrics.port must be between 1 and 65535");
            }
            result.port = static_cast<uint16_t>(*val);
        }
        if (auto val = (*metrics)["bind_address"].value<std::string>()) {
            result.bind_address = *val;
        }

        if (auto err = validateTableKeys(*metrics, {"enabled", "port", "bind_address"})) {
            return std::unexpected(std::move(*err));
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

        auto metrics = parseMetrics(tbl);
        if (!metrics)
            return std::unexpected(metrics.error());

        return Config{
            std::move(*logging),
            std::move(*engine),
            std::move(*threading),
            std::move(*metrics),
        };
    } catch (const toml::parse_error& err) {
        return std::unexpected(std::string{err.what()});
    }
}
