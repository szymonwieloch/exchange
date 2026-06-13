#include <gtest/gtest.h>

#include <expected>
#include <string>
#include <vector>

#include "lib/main/config.h"

using namespace exchange;

// ===================================================================
//  parseLogLevel
// ===================================================================

TEST(ParseLogLevelTest, RecognisesAllValidLevels) {
    EXPECT_EQ(parseLogLevel("debug"), utils::LogLevel::DEBUG);
    EXPECT_EQ(parseLogLevel("info"), utils::LogLevel::INFO);
    EXPECT_EQ(parseLogLevel("warn"), utils::LogLevel::WARN);
    EXPECT_EQ(parseLogLevel("error"), utils::LogLevel::ERROR);
}

TEST(ParseLogLevelTest, CaseSensitive) {
    // parseLogLevel uses exact string comparison — uppercase is NOT recognised
    EXPECT_EQ(parseLogLevel("DEBUG"), std::nullopt);
    EXPECT_EQ(parseLogLevel("Info"), std::nullopt);
    EXPECT_EQ(parseLogLevel("WARN"), std::nullopt);
    EXPECT_EQ(parseLogLevel("Error"), std::nullopt);
}

TEST(ParseLogLevelTest, UnknownValueReturnsNullopt) {
    EXPECT_EQ(parseLogLevel("trace"), std::nullopt);
    EXPECT_EQ(parseLogLevel(""), std::nullopt);
    EXPECT_EQ(parseLogLevel("warning"), std::nullopt);
    EXPECT_EQ(parseLogLevel("debugg"), std::nullopt);
}

// ===================================================================
//  parseLogging
// ===================================================================

TEST(ParseLoggingTest, DefaultsWhenSectionMissing) {
    const auto tbl = toml::parse("");
    auto result = parseLogging(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->level, utils::LogLevel::INFO);
    EXPECT_EQ(result->file, "exchange.log");
}

TEST(ParseLoggingTest, ParsesLevelAndFile) {
    const auto tbl = toml::parse(R"(
        [logging]
        level = "debug"
        file  = "/tmp/test.log"
    )");
    auto result = parseLogging(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->level, utils::LogLevel::DEBUG);
    EXPECT_EQ(result->file, "/tmp/test.log");
}

TEST(ParseLoggingTest, ParsesPartialSettings) {
    const auto tbl = toml::parse(R"(
        [logging]
        level = "warn"
    )");
    auto result = parseLogging(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->level, utils::LogLevel::WARN);
    EXPECT_EQ(result->file, "exchange.log");  // default preserved
}

TEST(ParseLoggingTest, ParsesOnlyFile) {
    const auto tbl = toml::parse(R"(
        [logging]
        file = "custom.log"
    )");
    auto result = parseLogging(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->level, utils::LogLevel::INFO);  // default preserved
    EXPECT_EQ(result->file, "custom.log");
}

TEST(ParseLoggingTest, ErrorOnUnknownLevel) {
    const auto tbl = toml::parse(R"(
        [logging]
        level = "trace"
    )");
    auto result = parseLogging(tbl);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("Unknown log level"), std::string::npos);
    EXPECT_NE(result.error().find("trace"), std::string::npos);
}

TEST(ParseLoggingTest, ErrorOnUnknownKey) {
    const auto tbl = toml::parse(R"(
        [logging]
        level = "info"
        colour = "blue"
    )");
    auto result = parseLogging(tbl);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("Unknown key"), std::string::npos);
    EXPECT_NE(result.error().find("colour"), std::string::npos);
}

// ===================================================================
//  parseEngine
// ===================================================================

TEST(ParseEngineTest, DefaultsWhenSectionMissing) {
    const auto tbl = toml::parse("");
    auto result = parseEngine(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->tickers.empty());
}

TEST(ParseEngineTest, ParsesTickersList) {
    const auto tbl = toml::parse(R"(
        [engine]
        tickers = ["AAPL", "GOOG", "MSFT"]
    )");
    auto result = parseEngine(tbl);
    ASSERT_TRUE(result.has_value());
    const std::vector<std::string> expected = {"AAPL", "GOOG", "MSFT"};
    EXPECT_EQ(result->tickers, expected);
}

TEST(ParseEngineTest, EmptyTickersList) {
    const auto tbl = toml::parse(R"(
        [engine]
        tickers = []
    )");
    auto result = parseEngine(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->tickers.empty());
}

TEST(ParseEngineTest, SingleTicker) {
    const auto tbl = toml::parse(R"(
        [engine]
        tickers = ["ONLY"]
    )");
    auto result = parseEngine(tbl);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->tickers.size(), 1u);
    EXPECT_EQ(result->tickers[0], "ONLY");
}

TEST(ParseEngineTest, ErrorOnUnknownKey) {
    const auto tbl = toml::parse(R"(
        [engine]
        tickers = ["AAPL"]
        max_orders = 1000
    )");
    auto result = parseEngine(tbl);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("Unknown key"), std::string::npos);
    EXPECT_NE(result.error().find("max_orders"), std::string::npos);
}

// ===================================================================
//  parseThreading
// ===================================================================

TEST(ParseThreadingTest, DefaultsWhenSectionMissing) {
    const auto tbl = toml::parse("");
    auto result = parseThreading(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->engine_core, -1);
    EXPECT_EQ(result->logger_core, -1);
}

TEST(ParseThreadingTest, ParsesBothCores) {
    const auto tbl = toml::parse(R"(
        [threading]
        engine_core = 2
        logger_core = 3
    )");
    auto result = parseThreading(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->engine_core, 2);
    EXPECT_EQ(result->logger_core, 3);
}

TEST(ParseThreadingTest, ParsesPartialSettings) {
    const auto tbl = toml::parse(R"(
        [threading]
        engine_core = 0
    )");
    auto result = parseThreading(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->engine_core, 0);
    EXPECT_EQ(result->logger_core, -1);  // default preserved
}

TEST(ParseThreadingTest, NegativeCoreValues) {
    const auto tbl = toml::parse(R"(
        [threading]
        engine_core = -1
        logger_core = -2
    )");
    auto result = parseThreading(tbl);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->engine_core, -1);
    EXPECT_EQ(result->logger_core, -2);
}

TEST(ParseThreadingTest, ErrorOnUnknownKey) {
    const auto tbl = toml::parse(R"(
        [threading]
        engine_core = 0
        priority = "high"
    )");
    auto result = parseThreading(tbl);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("Unknown key"), std::string::npos);
    EXPECT_NE(result.error().find("priority"), std::string::npos);
}

// ===================================================================
//  parseConfig – full integration
// ===================================================================

TEST(ParseConfigTest, ParsesCompleteValidConfig) {
    const auto tbl = toml::parse(R"(
        [logging]
        level = "warn"
        file  = "/var/log/exchange.log"

        [engine]
        tickers = ["AAPL", "GOOG"]

        [threading]
        engine_core = 0
        logger_core = 1
    )");

    auto logging = parseLogging(tbl);
    ASSERT_TRUE(logging.has_value());
    EXPECT_EQ(logging->level, utils::LogLevel::WARN);
    EXPECT_EQ(logging->file, "/var/log/exchange.log");

    auto engine = parseEngine(tbl);
    ASSERT_TRUE(engine.has_value());
    const std::vector<std::string> expected = {"AAPL", "GOOG"};
    EXPECT_EQ(engine->tickers, expected);

    auto threading = parseThreading(tbl);
    ASSERT_TRUE(threading.has_value());
    EXPECT_EQ(threading->engine_core, 0);
    EXPECT_EQ(threading->logger_core, 1);
}

TEST(ParseConfigTest, AllDefaultsWithEmptyConfig) {
    const auto tbl = toml::parse("");

    auto logging = parseLogging(tbl);
    ASSERT_TRUE(logging.has_value());
    EXPECT_EQ(logging->level, utils::LogLevel::INFO);
    EXPECT_EQ(logging->file, "exchange.log");

    auto engine = parseEngine(tbl);
    ASSERT_TRUE(engine.has_value());
    EXPECT_TRUE(engine->tickers.empty());

    auto threading = parseThreading(tbl);
    ASSERT_TRUE(threading.has_value());
    EXPECT_EQ(threading->engine_core, -1);
    EXPECT_EQ(threading->logger_core, -1);
}

TEST(ParseConfigTest, ErrorOnBadLogLevel) {
    const auto tbl = toml::parse(R"(
        [logging]
        level = "verbose"
    )");
    auto logging = parseLogging(tbl);
    ASSERT_FALSE(logging.has_value());
}

TEST(ParseConfigTest, CaseSensitiveLogLevel) {
    // Uppercase "ERROR" is not a recognised level — parseLogLevel is case-sensitive
    const auto tbl = toml::parse(R"(
        [logging]
        level = "ERROR"
    )");
    auto logging = parseLogging(tbl);
    ASSERT_FALSE(logging.has_value());
    EXPECT_NE(logging.error().find("Unknown log level"), std::string::npos);
}

TEST(ParseConfigTest, ErrorOnUnknownKeyInAnySection) {
    // Unknown key in [engine] should fail the full parseConfig pipeline
    const auto tbl = toml::parse(R"(
        [logging]
        level = "info"

        [engine]
        tickers = ["AAPL"]
        garbage = true

        [threading]
        engine_core = 0
    )");
    auto engine = parseEngine(tbl);
    ASSERT_FALSE(engine.has_value());
    EXPECT_NE(engine.error().find("Unknown key"), std::string::npos);
    EXPECT_NE(engine.error().find("garbage"), std::string::npos);
}
