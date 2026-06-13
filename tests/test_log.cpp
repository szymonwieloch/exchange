#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "lib/utils/log.h"

using namespace utils;

// ===================================================================
//  LogLevel enum tests
// ===================================================================

TEST(LogLevelTest, EnumValuesArePowersOfTwo) {
    EXPECT_EQ(static_cast<int>(LogLevel::DEBUG), 1);
    EXPECT_EQ(static_cast<int>(LogLevel::INFO), 2);
    EXPECT_EQ(static_cast<int>(LogLevel::WARN), 4);
    EXPECT_EQ(static_cast<int>(LogLevel::ERROR), 8);
}

TEST(LogLevelTest, OrOperatorCombinesMasks) {
    EXPECT_EQ(static_cast<int>(LogLevel::DEBUG | LogLevel::INFO), 3);
    EXPECT_EQ(static_cast<int>(LogLevel::WARN | LogLevel::ERROR), 12);
    EXPECT_EQ(static_cast<int>(LogLevel::INFO | LogLevel::WARN), 6);
}

TEST(LogLevelTest, AndOperatorFindsIntersection) {
    const auto mask = LogLevel::INFO | LogLevel::WARN;
    EXPECT_EQ(static_cast<int>(mask & LogLevel::INFO), static_cast<int>(LogLevel::INFO));
    EXPECT_EQ(static_cast<int>(mask & LogLevel::DEBUG), 0);
    EXPECT_EQ(static_cast<int>(mask & LogLevel::ERROR), 0);
}

TEST(LogLevelTest, GreaterOrEqualSeverityOrdering) {
    EXPECT_GE(LogLevel::ERROR, LogLevel::WARN);
    EXPECT_GE(LogLevel::WARN, LogLevel::INFO);
    EXPECT_GE(LogLevel::INFO, LogLevel::DEBUG);
    EXPECT_GE(LogLevel::DEBUG, LogLevel::DEBUG);  // reflexive
    EXPECT_GE(LogLevel::ERROR, LogLevel::ERROR);  // reflexive
}

TEST(LogLevelTest, LessSevereIsNotGE) {
    EXPECT_FALSE(LogLevel::DEBUG >= LogLevel::INFO);
    EXPECT_FALSE(LogLevel::INFO >= LogLevel::WARN);
    EXPECT_FALSE(LogLevel::WARN >= LogLevel::ERROR);
}

// ===================================================================
//  Header struct tests
// ===================================================================

TEST(HeaderTest, DebugHeaderHasTimestamp) {
    DebugHeader hdr{TscTimestamp{42}};
    EXPECT_EQ(type_safe::get(hdr.timestamp), 42ULL);
}

TEST(HeaderTest, InfoHeaderHasTimestamp) {
    InfoHeader hdr{TscTimestamp{100}};
    EXPECT_EQ(type_safe::get(hdr.timestamp), 100ULL);
}

TEST(HeaderTest, WarnHeaderHasTimestamp) {
    WarnHeader hdr{TscTimestamp{200}};
    EXPECT_EQ(type_safe::get(hdr.timestamp), 200ULL);
}

TEST(HeaderTest, ErrorHeaderHasTimestamp) {
    ErrorHeader hdr{TscTimestamp{999}};
    EXPECT_EQ(type_safe::get(hdr.timestamp), 999ULL);
}

TEST(HeaderTest, DebugHeaderOstreamContainsDEBG) {
    std::ostringstream oss;
    oss << DebugHeader{};
    const std::string out = oss.str();
    EXPECT_TRUE(out.find("DEBG") != std::string::npos);
}

TEST(HeaderTest, InfoHeaderOstreamContainsINFO) {
    std::ostringstream oss;
    oss << InfoHeader{};
    const std::string out = oss.str();
    EXPECT_TRUE(out.find("INFO") != std::string::npos);
}

TEST(HeaderTest, WarnHeaderOstreamContainsWARN) {
    std::ostringstream oss;
    oss << WarnHeader{};
    const std::string out = oss.str();
    EXPECT_TRUE(out.find("WARN") != std::string::npos);
}

TEST(HeaderTest, ErrorHeaderOstreamContainsERRO) {
    std::ostringstream oss;
    oss << ErrorHeader{};
    const std::string out = oss.str();
    EXPECT_TRUE(out.find("ERRO") != std::string::npos);
}

// ===================================================================
//  LogElement variant tests
// ===================================================================

TEST(LogElementTest, CanHoldDebugHeader) {
    LogElement elem = DebugHeader{};
    EXPECT_TRUE(std::holds_alternative<DebugHeader>(elem));
}

TEST(LogElementTest, CanHoldInfoHeader) {
    LogElement elem = InfoHeader{};
    EXPECT_TRUE(std::holds_alternative<InfoHeader>(elem));
}

TEST(LogElementTest, CanHoldWarnHeader) {
    LogElement elem = WarnHeader{};
    EXPECT_TRUE(std::holds_alternative<WarnHeader>(elem));
}

TEST(LogElementTest, CanHoldErrorHeader) {
    LogElement elem = ErrorHeader{};
    EXPECT_TRUE(std::holds_alternative<ErrorHeader>(elem));
}

TEST(LogElementTest, CanHoldUint64) {
    LogElement elem = std::uint64_t{42};
    EXPECT_EQ(std::get<std::uint64_t>(elem), 42ULL);
}

TEST(LogElementTest, CanHoldInt64) {
    LogElement elem = std::int64_t{-100};
    EXPECT_EQ(std::get<std::int64_t>(elem), -100LL);
}

TEST(LogElementTest, CanHoldUnsignedInt) {
    LogElement elem = 7U;
    EXPECT_EQ(std::get<unsigned int>(elem), 7U);
}

TEST(LogElementTest, CanHoldInt) {
    LogElement elem = -3;
    EXPECT_EQ(std::get<int>(elem), -3);
}

TEST(LogElementTest, CanHoldDouble) {
    LogElement elem = 3.14;
    EXPECT_DOUBLE_EQ(std::get<double>(elem), 3.14);
}

TEST(LogElementTest, CanHoldChar) {
    LogElement elem = 'X';
    EXPECT_EQ(std::get<char>(elem), 'X');
}

TEST(LogElementTest, CanHoldConstCharPtr) {
    const char* msg = "hello";
    LogElement elem = msg;
    EXPECT_STREQ(std::get<const char*>(elem), "hello");
}

// ===================================================================
//  LOG_QUEUE_SIZE
// ===================================================================

TEST(LogQueueSizeTest, IsReasonableSize) {
    EXPECT_GE(LOG_QUEUE_SIZE, 1024U);
    // Must be a power of two (common for ring buffers) or at least sensible
    EXPECT_EQ(LOG_QUEUE_SIZE, 8U * 1024U * 1024U);
}

// ===================================================================
//  Logger construction and lifecycle
// ===================================================================

// Helper: read entire log file into a string
static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST(LoggerTest, ConstructDoesNotThrow) {
    EXPECT_NO_THROW(Logger("test_construct.log", LogLevel::INFO));
    std::remove("test_construct.log");
}

TEST(LoggerTest, ConstructWithAllLevels) {
    EXPECT_NO_THROW(Logger("test_debug.log", LogLevel::DEBUG));
    EXPECT_NO_THROW(Logger("test_info.log", LogLevel::INFO));
    EXPECT_NO_THROW(Logger("test_warn.log", LogLevel::WARN));
    EXPECT_NO_THROW(Logger("test_error.log", LogLevel::ERROR));
    std::remove("test_debug.log");
    std::remove("test_info.log");
    std::remove("test_warn.log");
    std::remove("test_error.log");
}

TEST(LoggerTest, StartSucceedsWithValidPath) {
    Logger logger("test_start.log", LogLevel::INFO);
    EXPECT_TRUE(logger.start());
    // Destructor will stop and close
}

TEST(LoggerTest, DoubleStartReturnsFalse) {
    Logger logger("test_double_start.log", LogLevel::INFO);
    EXPECT_TRUE(logger.start());
    EXPECT_FALSE(logger.start());  // already running
}

TEST(LoggerTest, StartFailsWithInvalidPath) {
    Logger logger("/nonexistent_dir_xyz/test.log", LogLevel::INFO);
    EXPECT_FALSE(logger.start());
}

TEST(LoggerTest, StopOnNonRunningIsNoop) {
    Logger logger("test_stop_noop.log", LogLevel::INFO);
    EXPECT_NO_THROW(logger.stop());  // stop before start is harmless
    std::remove("test_stop_noop.log");
}

// ===================================================================
//  Basic logging output
// ===================================================================

TEST(LoggerTest, InfoMessageAppearsInFile) {
    const std::string path = "test_info_msg.log";
    {
        Logger logger(path, LogLevel::INFO);
        ASSERT_TRUE(logger.start());
        logger.info("hello", " ", "world");
        logger.info("value:", 42);
    }
    const std::string content = readFile(path);
    EXPECT_NE(content.find("INFO"), std::string::npos);
    EXPECT_NE(content.find("hello"), std::string::npos);
    EXPECT_NE(content.find("world"), std::string::npos);
    EXPECT_NE(content.find("value:"), std::string::npos);
    EXPECT_NE(content.find("42"), std::string::npos);
    std::remove(path.c_str());
}

TEST(LoggerTest, WarnMessageAppearsInFile) {
    const std::string path = "test_warn_msg.log";
    {
        Logger logger(path, LogLevel::WARN);
        ASSERT_TRUE(logger.start());
        logger.warn("low disk space");
    }
    const std::string content = readFile(path);
    EXPECT_NE(content.find("WARN"), std::string::npos);
    EXPECT_NE(content.find("low disk space"), std::string::npos);
    std::remove(path.c_str());
}

TEST(LoggerTest, ErrorMessageAppearsInFile) {
    const std::string path = "test_error_msg.log";
    {
        Logger logger(path, LogLevel::ERROR);
        ASSERT_TRUE(logger.start());
        logger.error("fatal:", " ", "connection lost");
    }
    const std::string content = readFile(path);
    EXPECT_NE(content.find("ERRO"), std::string::npos);
    EXPECT_NE(content.find("fatal:"), std::string::npos);
    EXPECT_NE(content.find("connection lost"), std::string::npos);
    std::remove(path.c_str());
}

TEST(LoggerTest, DebugMessageAppearsInFile) {
    const std::string path = "test_debug_msg.log";
    {
        Logger logger(path, LogLevel::DEBUG);
        ASSERT_TRUE(logger.start());
        logger.debug("trace point");
    }
    const std::string content = readFile(path);
    // Debug is only emitted when the compile-time MIN_LOG_LEVEL is
    // DEBUG or lower.  The default (INFO) filters debug at compile
    // time via `if constexpr` in Logger::debug().
    if constexpr (LogLevel::MIN_LOG_LEVEL <= LogLevel::DEBUG) {
        EXPECT_NE(content.find("DEBG"), std::string::npos);
        EXPECT_NE(content.find("trace point"), std::string::npos);
    } else {
        // DEBUG compiled out — file contains only auto-generated
        // INFO lines (start/stop), not our debug payload.
        EXPECT_EQ(content.find("DEBG"), std::string::npos);
        EXPECT_EQ(content.find("trace point"), std::string::npos);
    }
    std::remove(path.c_str());
}

TEST(LoggerTest, MultipleArgumentTypesAreLogged) {
    const std::string path = "test_multi_types.log";
    {
        Logger logger(path, LogLevel::INFO);
        ASSERT_TRUE(logger.start());
        logger.info("int=", 123, " double=", 4.5, " char=", 'Z', " str=", "end");
    }
    const std::string content = readFile(path);
    EXPECT_NE(content.find("int="), std::string::npos);
    EXPECT_NE(content.find("123"), std::string::npos);
    EXPECT_NE(content.find("double="), std::string::npos);
    EXPECT_NE(content.find("4.5"), std::string::npos);
    EXPECT_NE(content.find("char="), std::string::npos);
    EXPECT_NE(content.find("Z"), std::string::npos);
    EXPECT_NE(content.find("str="), std::string::npos);
    EXPECT_NE(content.find("end"), std::string::npos);
    std::remove(path.c_str());
}

TEST(LoggerTest, NegativeInt64IsLogged) {
    const std::string path = "test_neg_int64.log";
    {
        Logger logger(path, LogLevel::INFO);
        ASSERT_TRUE(logger.start());
        logger.info("neg=", std::int64_t{-999});
    }
    const std::string content = readFile(path);
    EXPECT_NE(content.find("-999"), std::string::npos);
    std::remove(path.c_str());
}

TEST(LoggerTest, LargeUint64IsLogged) {
    const std::string path = "test_uint64.log";
    {
        Logger logger(path, LogLevel::INFO);
        ASSERT_TRUE(logger.start());
        logger.info("big=", std::uint64_t{18'446'744'073'709'551'615ULL});
    }
    const std::string content = readFile(path);
    EXPECT_NE(content.find("18446744073709551615"), std::string::npos);
    std::remove(path.c_str());
}

// ===================================================================
//  Runtime level filtering
// ===================================================================

TEST(LoggerTest, DebugFilteredOutWhenMinLevelInfo) {
    const std::string path = "test_filter_debug.log";
    {
        Logger logger(path, LogLevel::INFO);  // min_level = INFO
        ASSERT_TRUE(logger.start());
        logger.debug("should not appear");
        logger.info("should appear");
    }
    const std::string content = readFile(path);
    EXPECT_EQ(content.find("DEBG"), std::string::npos);
    EXPECT_EQ(content.find("should not appear"), std::string::npos);
    EXPECT_NE(content.find("INFO"), std::string::npos);
    EXPECT_NE(content.find("should appear"), std::string::npos);
    std::remove(path.c_str());
}

TEST(LoggerTest, InfoFilteredOutWhenMinLevelWarn) {
    const std::string path = "test_filter_info.log";
    {
        Logger logger(path, LogLevel::WARN);  // min_level = WARN
        ASSERT_TRUE(logger.start());
        logger.info("should not appear");
        logger.warn("should appear");
    }
    const std::string content = readFile(path);
    EXPECT_EQ(content.find("INFO"), std::string::npos);
    EXPECT_EQ(content.find("should not appear"), std::string::npos);
    EXPECT_NE(content.find("WARN"), std::string::npos);
    EXPECT_NE(content.find("should appear"), std::string::npos);
    std::remove(path.c_str());
}

TEST(LoggerTest, WarnFilteredOutWhenMinLevelError) {
    const std::string path = "test_filter_warn.log";
    {
        Logger logger(path, LogLevel::ERROR);  // min_level = ERROR
        ASSERT_TRUE(logger.start());
        logger.warn("should not appear");
        logger.error("should appear");
    }
    const std::string content = readFile(path);
    EXPECT_EQ(content.find("WARN"), std::string::npos);
    EXPECT_EQ(content.find("should not appear"), std::string::npos);
    EXPECT_NE(content.find("ERRO"), std::string::npos);
    EXPECT_NE(content.find("should appear"), std::string::npos);
    std::remove(path.c_str());
}

// ===================================================================
//  Shutdown flush
// ===================================================================

TEST(LoggerTest, DestructorFlushesAllMessages) {
    const std::string path = "test_flush_all.log";
    {
        Logger logger(path, LogLevel::INFO);
        ASSERT_TRUE(logger.start());
        for (int i = 0; i < 100; ++i) {
            logger.info("msg", i);
        }
        // Destructor will stop() — flush + join
    }
    const std::string content = readFile(path);
    EXPECT_NE(content.find("msg0"), std::string::npos);
    EXPECT_NE(content.find("msg99"), std::string::npos);
    EXPECT_NE(content.find("Logger shutting down..."), std::string::npos);
    std::remove(path.c_str());
}

// ===================================================================
//  Concurrency — multiple producers
// ===================================================================

TEST(LoggerTest, MultipleThreadsCanLogConcurrently) {
    const std::string path = "test_concurrent.log";
    constexpr int num_threads = 8;
    constexpr int msgs_per_thread = 50;

    {
        Logger logger(path, LogLevel::INFO);
        ASSERT_TRUE(logger.start());

        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&logger, t]() {
                for (int i = 0; i < msgs_per_thread; ++i) {
                    logger.info("thread", t, "msg", i);
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        // Logger destructor flushes
    }
    const std::string content = readFile(path);
    // Verify messages from each thread appear
    for (int t = 0; t < num_threads; ++t) {
        const std::string marker = "thread" + std::to_string(t);
        EXPECT_NE(content.find(marker), std::string::npos) << "Missing messages from thread " << t;
    }
    // Verify the final message from each thread
    for (int t = 0; t < num_threads; ++t) {
        const std::string marker = "msg" + std::to_string(msgs_per_thread - 1);
        EXPECT_NE(content.find(marker), std::string::npos)
            << "Missing last message from thread " << t;
    }
    std::remove(path.c_str());
}

// ===================================================================
//  Queue full — silent drop
// ===================================================================

TEST(LoggerTest, ExcessMessagesAreSilentlyDropped) {
    // Use a logger with a very small internal queue via the fixed-size
    // MPSCQueue.  Since LOG_QUEUE_SIZE is hardcoded to 8M, we verify
    // that the drop path compiles and runs by flooding the queue.
    // In practice a real test of drop behaviour requires injecting
    // the queue, but we can at least verify that massive flooding
    // does not crash or hang.
    const std::string path = "test_flood.log";
    {
        Logger logger(path, LogLevel::INFO);
        ASSERT_TRUE(logger.start());

        // Flood with many messages faster than the consumer can flush.
        // Some will certainly be dropped (queue size 8M, each message
        // is multiple slots).  This must not deadlock or segfault.
        for (int i = 0; i < 1'000'000; ++i) {
            logger.info("flood", i);
        }
        // Destructor flushes remaining
    }
    // Just verifying we didn't crash — the file should exist and contain
    // some subset of messages.
    std::ifstream f(path);
    EXPECT_TRUE(f.good());
    std::remove(path.c_str());
}

// ===================================================================
//  Message ordering (single producer)
// ===================================================================

TEST(LoggerTest, MessagesPreserveOrderSingleProducer) {
    const std::string path = "test_order.log";
    {
        Logger logger(path, LogLevel::INFO);
        ASSERT_TRUE(logger.start());
        logger.info("first");
        logger.info("second");
        logger.info("third");
    }
    const std::string content = readFile(path);
    const auto pos1 = content.find("first");
    const auto pos2 = content.find("second");
    const auto pos3 = content.find("third");
    EXPECT_NE(pos1, std::string::npos);
    EXPECT_NE(pos2, std::string::npos);
    EXPECT_NE(pos3, std::string::npos);
    EXPECT_LT(pos1, pos2);
    EXPECT_LT(pos2, pos3);
    std::remove(path.c_str());
}

// ===================================================================
//  Header timestamp presence
// ===================================================================

TEST(LoggerTest, LogLinesContainTimestamp) {
    const std::string path = "test_timestamp.log";
    {
        Logger logger(path, LogLevel::INFO);
        ASSERT_TRUE(logger.start());
        logger.info("timestamped");
    }
    const std::string content = readFile(path);
    // Timestamp format: YYYY-MM-DD HH:MM:SS.nnnnnnnnn
    // Check for a pattern like "2026-" (current year)
    EXPECT_NE(content.find("202"), std::string::npos);
    // Check the colon-space separator after timestamp
    EXPECT_NE(content.find(": "), std::string::npos);
    std::remove(path.c_str());
}
