#pragma once
#include <atomic>
#include <cassert>
#include <chrono>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>

#include "die.h"
#include "queue.h"
#include "thread.h"

namespace utils {
constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;

enum class LogType : int8_t { NEW_LOG = 0, U64 = 1, I64 = 2, DOUBLE = 3, CHAR = 4, STR = 5 };

struct NewLog {
    const char *fmt;
};

using LogElement = std::variant<NewLog, std::uint64_t, std::int64_t, double, char, const char *>;

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

    void flushQueue() noexcept {
        while (running) {
            bool dirty = false;
            for (auto next = queue.getNextToRead(); queue.size() && next;
                 next = queue.getNextToRead()) {
                dirty = true;
                switch ((LogType)next->index()) {
                    case LogType::NEW_LOG:
                        file << '\n'
                             << std::chrono::system_clock::now() << ": "
                             << std::get<NewLog>(*next).fmt;
                        break;
                    case LogType::U64:
                        file << std::get<std::uint64_t>(*next);
                        break;
                    case LogType::I64:
                        file << std::get<std::int64_t>(*next);
                        break;
                    case LogType::DOUBLE:
                        file << std::get<double>(*next);
                        break;
                    case LogType::CHAR:
                        file << std::get<char>(*next);
                        break;
                    case LogType::STR:
                        file << std::get<const char *>(*next);
                        break;
                }
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

    /// Pieces of a format string, built once per log() call.
    ///
    /// The constructor splits @p s on every unescaped '%', collapsing '%%' → '%'.
    /// Because the constructor is constexpr, the compiler will constant-fold the
    /// result when called with a string literal — effectively free at runtime.
    template <size_t N>
    struct FormatStr final {
        char storage[N]{};
        /// Pointers to null-terminated pieces (worst-case: every char is a
        /// separate specifier).
        const char *pieces[N]{};
        /// Actual number of pieces (= number of unescaped '%' + 1).
        size_t pieceCount = 0;

        constexpr FormatStr(const char (&s)[N]) noexcept {
            size_t storePos = 0;

            pieces[pieceCount] = &storage[storePos];
            for (size_t i = 0; i < N - 1; ++i) {
                if (s[i] == '%') {
                    if (i + 1 < N - 1 && s[i + 1] == '%') {
                        storage[storePos++] = '%';  // collapse %% → %
                        ++i;
                    } else {
                        storage[storePos++] = '\0';  // terminate piece
                        ++pieceCount;
                        pieces[pieceCount] = &storage[storePos];
                    }
                } else {
                    storage[storePos++] = s[i];
                }
            }
            storage[storePos] = '\0';  // terminate last piece
            ++pieceCount;              // account for the final piece
        }
    };

    /// Push any LogElement-compatible value to the queue.
    void pushVal(const auto &val) noexcept {
        *(queue.getNextToWriteTo()) = val;
        queue.updateWriteIndex();
    }

    /// Plain string or format string with specifiers.
    ///
    /// The format string is parsed into pieces by FormatStr's constexpr
    /// constructor.  Argument-count mismatches are caught via assert in
    /// debug builds.
    template <size_t N, typename... A>
    auto log(const char (&s)[N], A... args) noexcept {
        const FormatStr<N> fmt{s};
        assert(fmt.pieceCount == 1 + sizeof...(A) &&
               "Number of '%' format specifiers must equal the number of arguments");
        logImpl<0>(fmt, args...);
    }

private:
    /// Push piece[Idx] (NewLog for Idx==0, otherwise const char*),
    /// then the value, then recurse for remaining piece/value pairs.
    template <size_t Idx, size_t SN, typename T, typename... A>
    auto logImpl(const FormatStr<SN> &fmt, const T &value, A... args) noexcept {
        if constexpr (Idx == 0)
            pushVal(NewLog{fmt.pieces[Idx]});
        else
            pushVal(fmt.pieces[Idx]);
        pushVal(value);
        logImpl<Idx + 1>(fmt, args...);
    }

    /// Base case: push the final piece (no more values remain).
    template <size_t Idx, size_t SN>
    auto logImpl(const FormatStr<SN> &fmt) noexcept {
        if constexpr (Idx == 0)
            pushVal(NewLog{fmt.pieces[Idx]});
        else
            pushVal(fmt.pieces[Idx]);
    }

private:
    const std::string file_name;
    std::ofstream file;
    LFQueue<LogElement> queue;
    std::atomic<bool> running = {true};
    std::thread logger_thread;
};

}  // namespace utils