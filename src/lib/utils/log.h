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
typedef int64_t Nanos;
constexpr Nanos NANOS_TO_MICROS = 1000;
constexpr Nanos MICROS_TO_MILLIS = 1000;
constexpr Nanos MILLIS_TO_SECS = 1000;
constexpr Nanos NANOS_TO_MILLIS = NANOS_TO_MICROS * MICROS_TO_MILLIS;
constexpr Nanos NANOS_TO_SECS = NANOS_TO_MILLIS * MILLIS_TO_SECS;
inline auto getCurrentNanos() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
inline auto &getCurrentTimeStr(std::string *time_str) {
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    time_str->assign(ctime(&time));
    if (!time_str->empty())
        time_str->at(time_str->length() - 1) = '\0';
    return *time_str;
}

constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;

enum class LogType : int8_t {
    CHAR = 0,
    INTEGER = 1,
    LONG_INTEGER = 2,
    LONG_LONG_INTEGER = 3,
    UNSIGNED_INTEGER = 4,
    UNSIGNED_LONG_INTEGER = 5,
    UNSIGNED_LONG_LONG_INTEGER = 6,
    FLOAT = 7,
    DOUBLE = 8
};

struct LogElement {
    LogType type = LogType::CHAR;
    union {
        char c;
        int i;
        long l;
        long long ll;
        unsigned u;
        unsigned long ul;
        unsigned long long ull;
        float f;
        double d;
    } u;
};

class Logger final {
    Logger() = delete;
    Logger(const Logger &) = delete;
    Logger(const Logger &&) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger &operator=(const Logger &&) = delete;

public:
    explicit Logger(const std::string &file_name) : file_name(file_name), queue(LOG_QUEUE_SIZE) {
        file.open(file_name);
        assert(file.is_open());
        // TODO: logger_thread = createAndStartThread(-1, "Common/Logger", [this]() { flushQueue();
        // });
        // assert(logger_thread != nullptr);
    }

    ~Logger() {
        std::cerr << "Flushing and closing Logger for " << file_name << std::endl;
        while (queue.size()) {
            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(1s);
        }
        running = false;
        logger_thread->join();
        file.close();
    }

    auto flushQueue() noexcept {
        while (running) {
            for (auto next = queue.getNextToRead(); queue.size() && next;
                 next = queue.getNextToRead()) {
                switch (next->type) {
                    case LogType::CHAR:
                        file << next->u.c;
                        break;
                    case LogType::INTEGER:
                        file << next->u.i;
                        break;
                    case LogType::LONG_INTEGER:
                        file << next->u.l;
                        break;
                    case LogType::LONG_LONG_INTEGER:
                        file << next->u.ll;
                        break;
                    case LogType::UNSIGNED_INTEGER:
                        file << next->u.u;
                        break;
                    case LogType::UNSIGNED_LONG_INTEGER:
                        file << next->u.ul;
                        break;
                    case LogType::UNSIGNED_LONG_LONG_INTEGER:
                        file << next->u.ull;
                        break;
                    case LogType::FLOAT:
                        file << next->u.f;
                        break;
                    case LogType::DOUBLE:
                        file << next->u.d;
                        break;
                }
                queue.updateReadIndex();
                next = queue.getNextToRead();
            }
            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(1ms);
        }
    }

    void pushValue(const LogElement &log_element) noexcept {
        *(queue.getNextToWriteTo()) = log_element;
        queue.updateWriteIndex();
    }

    void pushValue(const char value) noexcept {
        pushValue(LogElement{LogType::CHAR, {.c = value}});
    }

    void pushValue(const char *value) noexcept {
        while (*value) {
            pushValue(*value);
            ++value;
        }
    }

    void pushValue(const std::string &value) noexcept { pushValue(value.c_str()); }

    void pushValue(const int value) noexcept {
        pushValue(LogElement{LogType::INTEGER, {.i = value}});
    }
    void pushValue(const long value) noexcept {
        pushValue(LogElement{LogType::LONG_INTEGER, {.l = value}});
    }
    void pushValue(const long long value) noexcept {
        pushValue(LogElement{LogType::LONG_LONG_INTEGER, {.ll = value}});
    }
    void pushValue(const unsigned value) noexcept {
        pushValue(LogElement{LogType::UNSIGNED_INTEGER, {.u = value}});
    }
    void pushValue(const unsigned long value) noexcept {
        pushValue(LogElement{LogType::UNSIGNED_LONG_INTEGER, {.ul = value}});
    }
    void pushValue(const unsigned long long value) noexcept {
        pushValue(LogElement{LogType::UNSIGNED_LONG_LONG_INTEGER, {.ull = value}});
    }
    void pushValue(const float value) noexcept {
        pushValue(LogElement{LogType::FLOAT, {.f = value}});
    }
    void pushValue(const double value) noexcept {
        pushValue(LogElement{LogType::DOUBLE, {.d = value}});
    }

    template <typename T, typename... A>
    auto log(const char *s, const T &value, A... args) noexcept {
        while (*s) {
            if (*s == '%') {
                if (*(s + 1) == '%') [[unlikely]] {
                    ++s;
                } else {
                    pushValue(value);
                    log(s + 1, args...);
                    return;
                }
            }
            pushValue(*s++);
        }
        utils::die("extra arguments provided to log()");
    }

private:
    const std::string file_name;
    std::ofstream file;
    LFQueue<LogElement> queue;
    std::atomic<bool> running = {true};
    std::thread *logger_thread = nullptr;
};

}  // namespace utils