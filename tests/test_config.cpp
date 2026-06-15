#include <gtest/gtest.h>

#include <expected>
#include <string>
#include <vector>

#include "glaze/toml.hpp"
#include "lib/main/config.h"

// ── Helper: parse a TOML string into a Config ──────────────────────────

[[nodiscard]] static std::expected<Config, std::string> parseConfigString(
    const std::string& toml_str) noexcept {
    Config config;
    // An empty input is valid TOML — just return defaults.
    if (toml_str.empty()) {
        return config;
    }
    auto err =
        glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = true}>(config, toml_str);
    if (err) {
        return std::unexpected(glz::format_error(err, toml_str));
    }
    return config;
}

// ===================================================================
//  Logging section
// ===================================================================

TEST(LoggingTest, DefaultsWhenSectionMissing) {
    auto result = parseConfigString("");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->logging.level, utils::LogLevel::INFO);
    EXPECT_EQ(result->logging.file, "exchange.log");
}

TEST(LoggingTest, ParsesLevelAndFile) {
    auto result = parseConfigString(R"(
        [logging]
        level = "debug"
        file  = "/tmp/test.log"
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->logging.level, utils::LogLevel::DEBUG);
    EXPECT_EQ(result->logging.file, "/tmp/test.log");
}

TEST(LoggingTest, ParsesPartialSettings) {
    auto result = parseConfigString(R"(
        [logging]
        level = "warn"
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->logging.level, utils::LogLevel::WARN);
    EXPECT_EQ(result->logging.file, "exchange.log");  // default preserved
}

TEST(LoggingTest, ParsesOnlyFile) {
    auto result = parseConfigString(R"(
        [logging]
        file = "custom.log"
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->logging.level, utils::LogLevel::INFO);  // default preserved
    EXPECT_EQ(result->logging.file, "custom.log");
}

TEST(LoggingTest, ErrorOnUnknownLevel) {
    auto result = parseConfigString(R"(
        [logging]
        level = "trace"
    )");
    ASSERT_FALSE(result.has_value());
    // Error message should mention the enum parse failure
    EXPECT_NE(result.error().find("trace"), std::string::npos);
}

TEST(LoggingTest, ErrorOnUnknownKey) {
    auto result = parseConfigString(R"(
        [logging]
        level = "info"
        colour = "blue"
    )");
    ASSERT_FALSE(result.has_value());
    // Error message should mention the unknown key
    EXPECT_NE(result.error().find("colour"), std::string::npos);
}

// ===================================================================
//  Engine section
// ===================================================================

TEST(EngineTest, DefaultsWhenSectionMissing) {
    auto result = parseConfigString("");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->engine.tickers.empty());
}

TEST(EngineTest, ParsesTickersList) {
    auto result = parseConfigString(R"(
        [engine]
        tickers = ["AAPL", "GOOG", "MSFT"]
    )");
    ASSERT_TRUE(result.has_value());
    const std::vector<std::string> expected = {"AAPL", "GOOG", "MSFT"};
    EXPECT_EQ(result->engine.tickers, expected);
}

TEST(EngineTest, EmptyTickersList) {
    auto result = parseConfigString(R"(
        [engine]
        tickers = []
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->engine.tickers.empty());
}

TEST(EngineTest, SingleTicker) {
    auto result = parseConfigString(R"(
        [engine]
        tickers = ["ONLY"]
    )");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->engine.tickers.size(), 1u);
    EXPECT_EQ(result->engine.tickers[0], "ONLY");
}

TEST(EngineTest, ErrorOnUnknownKey) {
    auto result = parseConfigString(R"(
        [engine]
        tickers = ["AAPL"]
        max_orders = 1000
    )");
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("max_orders"), std::string::npos);
}

// ===================================================================
//  Threading section
// ===================================================================

TEST(ThreadingTest, DefaultsWhenSectionMissing) {
    auto result = parseConfigString("");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->threading.engine_core, -1);
    EXPECT_EQ(result->threading.logger_core, -1);
}

TEST(ThreadingTest, ParsesBothCores) {
    auto result = parseConfigString(R"(
        [threading]
        engine_core = 2
        logger_core = 3
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->threading.engine_core, 2);
    EXPECT_EQ(result->threading.logger_core, 3);
}

TEST(ThreadingTest, ParsesPartialSettings) {
    auto result = parseConfigString(R"(
        [threading]
        engine_core = 0
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->threading.engine_core, 0);
    EXPECT_EQ(result->threading.logger_core, -1);  // default preserved
}

TEST(ThreadingTest, NegativeCoreValues) {
    auto result = parseConfigString(R"(
        [threading]
        engine_core = -1
        logger_core = -2
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->threading.engine_core, -1);
    EXPECT_EQ(result->threading.logger_core, -2);
}

TEST(ThreadingTest, ErrorOnUnknownKey) {
    auto result = parseConfigString(R"(
        [threading]
        engine_core = 0
        priority = "high"
    )");
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("priority"), std::string::npos);
}

// ===================================================================
//  Metrics section
// ===================================================================

TEST(MetricsTest, DefaultsWhenSectionMissing) {
    auto result = parseConfigString("");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->metrics.enabled, false);
    EXPECT_EQ(result->metrics.port, 9090);
    EXPECT_EQ(result->metrics.bind_address, "127.0.0.1");
}

TEST(MetricsTest, ParsesFullSettings) {
    auto result = parseConfigString(R"(
        [metrics]
        enabled = true
        port = 8080
        bind_address = "0.0.0.0"
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->metrics.enabled, true);
    EXPECT_EQ(result->metrics.port, 8080);
    EXPECT_EQ(result->metrics.bind_address, "0.0.0.0");
}

TEST(MetricsTest, ParsesPartialSettings) {
    auto result = parseConfigString(R"(
        [metrics]
        enabled = true
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->metrics.enabled, true);
    EXPECT_EQ(result->metrics.port, 9090);                 // default preserved
    EXPECT_EQ(result->metrics.bind_address, "127.0.0.1");  // default preserved
}

TEST(MetricsTest, ErrorOnUnknownKey) {
    auto result = parseConfigString(R"(
        [metrics]
        enabled = true
        path = "/stats"
    )");
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("path"), std::string::npos);
}

// ===================================================================
//  Full config integration
// ===================================================================

TEST(ParseConfigTest, ParsesCompleteValidConfig) {
    auto result = parseConfigString(R"(
        [logging]
        level = "warn"
        file  = "/var/log/exchange.log"

        [engine]
        tickers = ["AAPL", "GOOG"]

        [threading]
        engine_core = 0
        logger_core = 1
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->logging.level, utils::LogLevel::WARN);
    EXPECT_EQ(result->logging.file, "/var/log/exchange.log");
    EXPECT_EQ(result->engine.tickers, (std::vector<std::string>{"AAPL", "GOOG"}));
    EXPECT_EQ(result->threading.engine_core, 0);
    EXPECT_EQ(result->threading.logger_core, 1);
}

TEST(ParseConfigTest, AllDefaultsWithEmptyConfig) {
    auto result = parseConfigString("");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->logging.level, utils::LogLevel::INFO);
    EXPECT_EQ(result->logging.file, "exchange.log");
    EXPECT_TRUE(result->engine.tickers.empty());
    EXPECT_EQ(result->threading.engine_core, -1);
    EXPECT_EQ(result->threading.logger_core, -1);
    EXPECT_EQ(result->metrics.enabled, false);
    EXPECT_EQ(result->metrics.port, 9090);
}

TEST(ParseConfigTest, ErrorOnBadLogLevel) {
    auto result = parseConfigString(R"(
        [logging]
        level = "verbose"
    )");
    ASSERT_FALSE(result.has_value());
}

TEST(ParseConfigTest, CaseSensitiveLogLevel) {
    // Uppercase "ERROR" is not a recognised enum value (case-sensitive)
    auto result = parseConfigString(R"(
        [logging]
        level = "ERROR"
    )");
    ASSERT_FALSE(result.has_value());
}

TEST(ParseConfigTest, ErrorOnUnknownKeyInAnySection) {
    auto result = parseConfigString(R"(
        [logging]
        level = "info"

        [engine]
        tickers = ["AAPL"]
        garbage = true

        [threading]
        engine_core = 0
    )");
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("garbage"), std::string::npos);
}

TEST(ParseConfigTest, AllSectionsTogether) {
    auto result = parseConfigString(R"(
        [logging]
        level = "error"
        file  = "prod.log"

        [engine]
        tickers = ["BTC", "ETH", "SOL"]

        [threading]
        engine_core = 3
        logger_core = 4

        [metrics]
        enabled = true
        port = 9999
        bind_address = "10.0.0.1"
    )");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->logging.level, utils::LogLevel::ERROR);
    EXPECT_EQ(result->logging.file, "prod.log");
    EXPECT_EQ(result->engine.tickers, (std::vector<std::string>{"BTC", "ETH", "SOL"}));
    EXPECT_EQ(result->threading.engine_core, 3);
    EXPECT_EQ(result->threading.logger_core, 4);
    EXPECT_EQ(result->metrics.enabled, true);
    EXPECT_EQ(result->metrics.port, 9999);
    EXPECT_EQ(result->metrics.bind_address, "10.0.0.1");
}
