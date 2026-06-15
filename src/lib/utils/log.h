#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <string>
#include <thread>

#include "queue.h"
#include "thread_utils.h"
#include "time.h"

namespace utils {

/// Granularity levels for log messages.
///
/// Each level is a power of two so they can be combined with bitwise OR
/// to form a mask (e.g. `WARN | ERROR`).  The `>=` operator defines
/// a severity ordering: DEBUG < INFO < WARN < ERROR.
enum struct LogLevel {
    DEBUG = 1 << 0,  ///< Detailed diagnostics, disabled in Release builds by default.
    INFO = 1 << 1,   ///< Informational messages (startup, shutdown, configuration).
    WARN = 1 << 2,   ///< Recoverable anomalies or degraded operation.
    ERROR = 1 << 3,  ///< Unrecoverable conditions; the system may need to halt.
};

/// Compile-time minimum log level.
///
/// Messages below this level are eliminated at compile time via `if constexpr`,
/// incurring zero runtime cost.  Set via the preprocessor, e.g.:
///
///     -DMIN_LOG_LEVEL=WARN
///
/// Defaults to INFO when not specified on the command line.
#ifndef MIN_LOG_LEVEL
#define MIN_LOG_LEVEL INFO
#endif  // MIN_LOG_LEVEL

/// Combines two log-level masks with bitwise OR (e.g. for filtering).
inline LogLevel operator|(LogLevel a, LogLevel b) {
    return static_cast<LogLevel>(static_cast<int>(a) | static_cast<int>(b));
}

/// Combines two log-level masks with bitwise AND (e.g. for testing membership).
inline LogLevel operator&(LogLevel a, LogLevel b) {
    return static_cast<LogLevel>(static_cast<int>(a) & static_cast<int>(b));
}

/// Severity comparison.  Returns true when @p a is at least as severe as @p b.
/// Used for runtime level filtering: `if (min_level >= WARN)`.
inline constexpr bool operator>=(LogLevel a, LogLevel b) {
    return static_cast<int>(a) >= static_cast<int>(b);
}

/// Strict severity comparison.  Returns true when @p a is strictly more severe than @p b.
/// Used for compile-time filtering: `if constexpr (MIN_LOG_LEVEL > WARN)`.
inline constexpr bool operator>(LogLevel a, LogLevel b) {
    return static_cast<int>(a) > static_cast<int>(b);
}

/// Capacity of the internal MPSC ring buffer, in number of LogElement slots.
///
/// 8M slots ≈ 32–64 MiB of backing storage (depending on variant size).
/// When the queue is full, new messages are silently dropped — size this
/// conservatively at startup to avoid data loss under burst load.
constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;

/// Header written before each DEBUG-level message payload.
struct DebugHeader {
    TscTimestamp timestamp;
};

/// Header written before each INFO-level message payload.
struct InfoHeader {
    TscTimestamp timestamp;
};

/// Header written before each WARN-level message payload.
struct WarnHeader {
    TscTimestamp timestamp;
};

/// Header written before each ERROR-level message payload.
struct ErrorHeader {
    TscTimestamp timestamp;
};

/// Concept constraining to the four log-header types.
template <typename Hdr>
concept LogHeader = std::same_as<Hdr, DebugHeader> || std::same_as<Hdr, InfoHeader> ||
                    std::same_as<Hdr, WarnHeader> || std::same_as<Hdr, ErrorHeader>;

/// Writes a log-line prefix with a formatted TSC timestamp.
/// Output: `\nLEVEL YYYY-MM-DD HH:MM:SS.nnnnnnnnn: `
/// where LEVEL is DEBG / INFO / WARN / ERRO depending on the header type.
template <LogHeader Hdr>
inline std::ostream &operator<<(std::ostream &os, const Hdr &hdr) {
    if constexpr (std::same_as<Hdr, DebugHeader>)
        os << "\nDEBG ";
    else if constexpr (std::same_as<Hdr, InfoHeader>)
        os << "\nINFO ";
    else if constexpr (std::same_as<Hdr, WarnHeader>)
        os << "\nWARN ";
    else if constexpr (std::same_as<Hdr, ErrorHeader>)
        os << "\nERRO ";
    os << hdr.timestamp << ": ";
    return os;
}

/// A single element in the log queue — either a level header or a user-supplied
/// argument.  The queue stores a sequence: [Header, arg1, arg2, ...] which the
/// consumer thread flattens via `operator<<` on each variant alternative.
using LogElement =
    std::variant<DebugHeader, InfoHeader, WarnHeader, ErrorHeader, std::uint64_t, std::int64_t,
                 std::uint32_t, std::int32_t, double, char, const char *>;

/// Asynchronous, lock-free file logger.
///
/// All `debug()`, `info()`, `warn()`, and `error()` calls are wait-free for
/// the caller — the message is written into a pre-allocated MPSC ring buffer
/// and a dedicated background thread flushes it to disk.  No heap allocations
/// occur in the logging hot path.
///
/// @par Compile-time filtering
/// Messages below the compile-time threshold (`MIN_LOG_LEVEL` macro) are
/// eliminated entirely via `if constexpr`, incurring zero runtime overhead.
///
/// @par Runtime filtering
/// The `min_level` constructor parameter provides an additional runtime gate.
/// For example, a Release binary may compile with `MIN_LOG_LEVEL=DEBUG` but
/// pass `LogLevel::WARN` at runtime to only see warnings and errors.
///
/// @par Thread safety
/// The ring buffer (MPSCQueue) is multi-producer safe — any thread may call
/// the logging methods concurrently.  The consumer thread reads from the queue
/// and writes to the output file sequentially.
///
/// @warning  When the ring buffer is full, messages are silently dropped.
///           Size `LOG_QUEUE_SIZE` to handle the maximum expected burst.
class Logger final {
    Logger() = delete;
    Logger(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;
    Logger &operator=(const Logger &&) = delete;

public:
    /// Constructs a logger that writes to @p file_name.
    ///
    /// @param file_name  Path to the output log file (will be overwritten).
    /// @param min_level  Messages below this severity are discarded at runtime.
    explicit Logger(const std::string &file_name, LogLevel min_level)
        : file_name(file_name), queue(LOG_QUEUE_SIZE), min_level(min_level) {}

    /// Flushes remaining messages and stops the background thread.
    ///
    /// Blocks until the output queue is empty, then joins the consumer thread
    /// and closes the file.  New log calls after destruction are UB.
    ~Logger() { stop(); }

    /// Opens the log file and launches the background consumer thread.
    ///
    /// @return true if the logger started successfully, false if already
    ///         running or the file could not be opened.
    [[nodiscard]] bool start() noexcept {
        if (running) {
            return false;
        }
        (void)globalCalibration();  // ensure calibration is triggered
        file.open(file_name);
        if (!file.is_open()) {
            return false;
        }
        running = true;
        logger_thread = std::thread([this]() { run(); });
        return true;
    }

    /// Flushes remaining messages and stops the background thread.
    ///
    /// Logs a final INFO message, drains the output queue, sets the
    /// running flag to false, joins the consumer thread, and closes
    /// the output file.  Safe to call multiple times; subsequent calls
    /// are no-ops once the logger has been stopped.
    void stop() noexcept {
        if (!running) {
            return;
        }
        info("Logger shutting down...");
        while (queue.size()) {
            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(10ms);
        }
        running = false;
        logger_thread.join();
        file.close();
    }

    /// Logs a WARN-level message.
    ///
    /// Each argument is written as a separate element in the ring buffer.
    /// Supported types are those listed in `LogElement` (integers, double,
    /// char, const char*).  The timestamp is captured at call time.
    template <typename... A>
    void warn(A... args) noexcept {
        if constexpr (LogLevel::MIN_LOG_LEVEL > LogLevel::WARN) {
            return;
        }
        if (min_level > LogLevel::WARN) {
            return;
        }
        log<utils::WarnHeader>(args...);
    }

    /// Logs an INFO-level message.
    ///
    /// Each argument is written as a separate element in the ring buffer.
    /// Supported types are those listed in `LogElement` (integers, double,
    /// char, const char*).  The timestamp is captured at call time.
    template <typename... A>
    void info(A... args) noexcept {
        if constexpr (LogLevel::MIN_LOG_LEVEL > LogLevel::INFO) {
            return;
        }
        if (min_level > LogLevel::INFO) {
            return;
        }
        log<utils::InfoHeader>(args...);
    }

    /// Logs a DEBUG-level message.
    ///
    /// Each argument is written as a separate element in the ring buffer.
    /// Supported types are those listed in `LogElement` (integers, double,
    /// char, const char*).  The timestamp is captured at call time.
    template <typename... A>
    void debug(A... args) noexcept {
        if constexpr (LogLevel::MIN_LOG_LEVEL > LogLevel::DEBUG) {
            return;
        }
        if (min_level > LogLevel::DEBUG) {
            return;
        }
        log<utils::DebugHeader>(args...);
    }

    /// Logs an ERROR-level message.
    ///
    /// Each argument is written as a separate element in the ring buffer.
    /// Supported types are those listed in `LogElement` (integers, double,
    /// char, const char*).  The timestamp is captured at call time.
    template <typename... A>
    void error(A... args) noexcept {
        if constexpr (LogLevel::MIN_LOG_LEVEL > LogLevel::ERROR) {
            return;
        }
        if (min_level > LogLevel::ERROR) {
            return;
        }
        log<utils::ErrorHeader>(args...);
    }

private:
    /// Consumer loop: reads elements from the queue and writes them to the
    /// output file via `operator<<`.  Sleeps for 1 ms when the queue is
    /// empty to avoid busy-waiting.  Flushes the file stream after each
    /// batch of messages.
    void run() noexcept {
        if (min_level <= LogLevel::INFO) {
            file << "\nINFO " << readTsc() << ": Logger started";
        }
        while (running) {
            bool dirty = false;
            for (auto next = queue.getNextToRead(); queue.size() && next;
                 next = queue.getNextToRead()) {
                dirty = true;
                std::visit([this](auto val) { file << val; }, *next);
                queue.updateReadIndex();
            }
            if (dirty) {
                file.flush();
                dirty = false;
            } else {
                using namespace std::literals::chrono_literals;
                std::this_thread::sleep_for(1ms);
            }
        }
    }

    /// Core logging primitive.  Reserves @p n contiguous slots (1 header +
    /// all arguments), writes a timestamped header into the first slot, then
    /// unpacks the variadic arguments into subsequent slots before committing.
    ///
    /// If the queue cannot accommodate the message, it is silently dropped.
    template <typename Hdr, typename... A>
    void log(A... args) noexcept {
        constexpr size_t n = 1 + sizeof...(args);  // header + all arguments
        const auto start = queue.reserve(n);
        if (start == static_cast<size_t>(-1)) [[unlikely]]
            return;  // queue full — drop message

        const auto cap = queue.capacity();
        *queue.slot(start) = Hdr{.timestamp = readTsc()};
        writeArgs((start + 1) % cap, args...);
        queue.commit(start, n);
    }

    /// Base case: writes a single argument into the slot at @p idx.
    template <typename T>
    void writeArgs(size_t idx, const T val) noexcept {
        *queue.slot(idx) = val;
    }

    /// Recursive case: writes @p first into the slot at @p idx, then
    /// recurses to handle the remaining arguments in subsequent slots.
    template <typename T, typename... A>
    void writeArgs(size_t idx, const T first, const A... rest) noexcept {
        *queue.slot(idx) = first;
        writeArgs((idx + 1) % queue.capacity(), rest...);
    }

private:
    const std::string file_name;          ///< Output file path.
    std::ofstream file;                   ///< Output file stream (opened by start()).
    MPSCQueue<LogElement> queue;          ///< Lock-free ring buffer for log elements.
    std::atomic<bool> running = {false};  ///< Set to true while the consumer thread is active.
    std::thread logger_thread;            ///< Background consumer thread.
    LogLevel min_level = LogLevel::INFO;  ///< Runtime minimum severity threshold.
};

}  // namespace utils