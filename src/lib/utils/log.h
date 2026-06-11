#pragma once
#include <atomic>
#include <cassert>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <string>
#include <thread>

#include "die.h"
#include "queue.h"
#include "thread.h"

namespace utils {

enum struct LogLevel {
    DEBUG = 1 << 0,
    INFO = 1 << 1,
    WARN = 1 << 2,
    ERROR = 1 << 3,
};

#ifndef MIN_LOG_LEVEL
#define MIN_LOG_LEVEL INFO
#endif  // MIN_LOG_LEVEL

inline LogLevel operator|(LogLevel a, LogLevel b) {
    return static_cast<LogLevel>(static_cast<int>(a) | static_cast<int>(b));
}

inline LogLevel operator&(LogLevel a, LogLevel b) {
    return static_cast<LogLevel>(static_cast<int>(a) & static_cast<int>(b));
}

inline bool operator>=(LogLevel a, LogLevel b) {
    return static_cast<int>(a) >= static_cast<int>(b);
}

constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;

using LogTimestamp = std::chrono::high_resolution_clock::time_point;

struct DebugHeader {
    LogTimestamp timestamp;
};

inline std::ostream &operator<<(std::ostream &os, const DebugHeader &hdr) {
    return os << '\n' << "DEBG " << hdr.timestamp << ':';
}

struct InfoHeader {
    LogTimestamp timestamp;
};

inline std::ostream &operator<<(std::ostream &os, const InfoHeader &hdr) {
    return os << '\n' << "INFO " << hdr.timestamp << ':';
}

struct WarnHeader {
    LogTimestamp timestamp;
};

inline std::ostream &operator<<(std::ostream &os, const WarnHeader &hdr) {
    return os << '\n' << "WARN " << hdr.timestamp << ':';
}

struct ErrorHeader {
    LogTimestamp timestamp;
};

inline std::ostream &operator<<(std::ostream &os, const ErrorHeader &hdr) {
    return os << '\n' << "ERRO " << hdr.timestamp << ':';
}

using LogElement = std::variant<DebugHeader, InfoHeader, WarnHeader, ErrorHeader, std::uint64_t,
                                std::int64_t, unsigned int, int, double, char, const char *>;

class Logger final {
    Logger() = delete;
    Logger(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;
    Logger &operator=(const Logger &&) = delete;

public:
    explicit Logger(const std::string &file_name) : file_name(file_name), queue(LOG_QUEUE_SIZE) {
        file.open(file_name);
        if (!file.is_open()) {
            die("Could not open log file");
        }
        logger_thread = std::thread([this]() { flushQueue(); });
    }

    ~Logger() {
        std::cerr << "Flushing and closing Logger for " << file_name << std::endl;
        while (queue.size()) {
            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(10ms);
        }
        running = false;
        logger_thread.join();
        file.close();
    }

    template <typename... A>
    void warn(A... args) noexcept {
        if constexpr (LogLevel::MIN_LOG_LEVEL > LogLevel::WARN) {
            return;
        }
        log<utils::WarnHeader>(args...);
    }

    template <typename... A>
    void info(A... args) noexcept {
        if constexpr (LogLevel::MIN_LOG_LEVEL > LogLevel::INFO) {
            return;
        }
        log<utils::InfoHeader>(args...);
    }

    template <typename... A>
    void debug(A... args) noexcept {
        if constexpr (LogLevel::MIN_LOG_LEVEL > LogLevel::DEBUG) {
            return;
        }
        log<utils::DebugHeader>(args...);
    }

    template <typename... A>
    void error(A... args) noexcept {
        if constexpr (LogLevel::MIN_LOG_LEVEL > LogLevel::ERROR) {
            return;
        }
        log<utils::ErrorHeader>(args...);
    }

private:
    void flushQueue() noexcept {
        while (running) {
            bool dirty = false;
            for (auto next = queue.getNextToRead(); queue.size() && next;
                 next = queue.getNextToRead()) {
                dirty = true;
                std::visit([this](auto val) { file << val; }, *next);
                queue.updateReadIndex();
                next = queue.getNextToRead();
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

    template <typename Hdr, typename... A>
    void log(A... args) noexcept {
        // TODO: reserve n places in the queue
        auto hdr = Hdr{.timestamp = std::chrono::high_resolution_clock::now()};
        pushVal(hdr);
        logImpl(args...);
    }

    template <typename T>
    void logImpl(const T val) noexcept {
        pushVal(val);
    }

    template <typename T, typename... A>
    void logImpl(const T first, const A... rest) noexcept {
        pushVal(first);
        logImpl(rest...);
    }

    void pushVal(const auto val) noexcept {
        *(queue.getNextToWriteTo()) = val;
        queue.updateWriteIndex();
    }

private:
    const std::string file_name;
    std::ofstream file;
    LFQueue<LogElement> queue;
    std::atomic<bool> running = {true};
    std::thread logger_thread;
};

}  // namespace utils