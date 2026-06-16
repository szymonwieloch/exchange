#pragma once

#include <cstdint>
#include <expected>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "glaze/toml.hpp"
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
        std::optional<int> engine_core;
        std::optional<int> logger_core;
        std::optional<int> response_thread_core;
    };

    /// Prometheus metrics server configuration.
    struct Metrics {
        bool enabled = false;
        uint16_t port = 9090;
        std::string bind_address = "127.0.0.1";
    };

    /// FIX protocol gateway configuration.
    struct Fix {
        bool enabled = false;
        uint16_t port = 5001;
        std::string bind_address = "0.0.0.0";
        uint32_t num_threads = 0;  // 0 = auto (hardware concurrency)
        std::string sender_comp_id = "CLIENT";
        std::string target_comp_id = "EXCHANGE";
        uint32_t heartbeat_interval = 30;
    };

    Logging logging;
    Engine engine;
    Threading threading;
    Metrics metrics;
    Fix fix;
};

// ── glaze metadata ──────────────────────────────────────────────────────

template <>
struct glz::meta<utils::LogLevel> {
    using enum utils::LogLevel;
    static constexpr auto value =
        glz::enumerate("debug", DEBUG, "info", INFO, "warn", WARN, "error", ERROR);
};

template <>
struct glz::meta<Config::Logging> {
    using T = Config::Logging;
    static constexpr auto value = glz::object(&T::level, &T::file);
};

template <>
struct glz::meta<Config::Engine> {
    using T = Config::Engine;
    static constexpr auto value = glz::object(&T::tickers);
};

template <>
struct glz::meta<Config::Threading> {
    using T = Config::Threading;
    static constexpr auto value =
        glz::object(&T::engine_core, &T::logger_core, &T::response_thread_core);
};

template <>
struct glz::meta<Config::Metrics> {
    using T = Config::Metrics;
    static constexpr auto value = glz::object(&T::enabled, &T::port, &T::bind_address);
};

template <>
struct glz::meta<Config::Fix> {
    using T = Config::Fix;
    static constexpr auto value =
        glz::object(&T::enabled, &T::port, &T::bind_address, &T::num_threads, &T::sender_comp_id,
                    &T::target_comp_id, &T::heartbeat_interval);
};

template <>
struct glz::meta<Config> {
    using T = Config;
    static constexpr auto value =
        glz::object(&T::logging, &T::engine, &T::threading, &T::metrics, &T::fix);
};

// ── Parse helpers ───────────────────────────────────────────────────────

/// Parses a TOML configuration file into a @ref Config struct.
///
/// @param path  Path to the TOML file.
/// @return  Config on success; error string on parse/validation failure.
[[nodiscard]] inline std::expected<Config, std::string> parseConfig(
    const std::string& path) noexcept {
    // Read file into a string buffer
    std::ifstream file(path);
    if (!file) {
        return std::unexpected("Cannot open config file: " + path);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string buffer = ss.str();

    Config config;

    // An empty TOML file is valid — all defaults apply.
    if (buffer.empty()) {
        return config;
    }

    auto err =
        glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = true}>(config, buffer);
    if (err) {
        return std::unexpected(glz::format_error(err, buffer));
    }

    // Port range validation (glaze does not enforce 1–65535 for uint16_t)
    if (config.metrics.port < 1) {
        return std::unexpected("metrics.port must be between 1 and 65535");
    }

    return config;
}
