#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "lib/exchange/book.h"
#include "lib/exchange/constants.h"
#include "lib/exchange/definitions.h"
#include "lib/exchange/engine.h"
#include "lib/exchange/md.h"
#include "lib/exchange/request.h"
#include "lib/main/config.h"
#include "lib/utils/die.h"
#include "lib/utils/log.h"
#include "lib/utils/mem.h"
#include "lib/utils/queue.h"
#include "lib/utils/thread.h"

namespace {

/// Parses a path to the configuration file from command-line arguments.
///
/// Usage: `exchange [config.toml]`
/// Returns the user-supplied path or the default `"config.toml"`.
[[nodiscard]] std::string parseConfigPath(int argc, char* argv[]) {
    if (argc > 2) {
        utils::die("Usage: exchange [config.toml]");
    }
    if (argc == 2) {
        return argv[1];
    }
    return "config.toml";
}

}  // namespace

int main(int argc, char* argv[]) {
    // ── Parse configuration ───────────────────────────────────────
    const auto config_path = parseConfigPath(argc, argv);
    std::cout << "Loading configuration from: " << config_path << std::endl;

    const auto config_result = exchange::parseConfig(config_path);
    if (!config_result) {
        utils::die("Failed to parse config: " + config_result.error());
    }
    const auto& config = *config_result;
    std::cout << "Log level: "
              << (config.logging.level >= utils::LogLevel::ERROR  ? "error"
                  : config.logging.level >= utils::LogLevel::WARN ? "warn"
                  : config.logging.level >= utils::LogLevel::INFO ? "info"
                                                                  : "debug")
              << std::endl;
    std::cout << "Log file:  " << config.logging.file << std::endl;
    std::cout << "Tickers:   " << config.engine.tickers.size() << std::endl;

    // ── Create lock-free communication queues ──────────────────────
    auto request_queue = exchange::RequestLFQueue(exchange::MAX_USER_UPDATES);
    auto response_queue = exchange::ResponseLFQueue(exchange::MAX_USER_UPDATES);
    auto md_queue = exchange::MDLFQueue(exchange::MAX_MARKET_UPDATES);

    // ── Instantiate the matching engine ────────────────────────────
    exchange::MatchingEngine engine(&request_queue, &response_queue, &md_queue, config.logging.file,
                                    config.logging.level);

    // ── Apply thread affinity (if configured) ──────────────────────
    if (config.threading.engine_core >= 0) {
        if (!utils::setThreadCore(config.threading.engine_core)) {
            std::cerr << "Warning: failed to pin engine thread to core "
                      << config.threading.engine_core << std::endl;
        } else {
            std::cout << "Engine pinned to core " << config.threading.engine_core << std::endl;
        }
    }

    // ── Print ticker registration ──────────────────────────────────
    for (const auto& ticker : config.engine.tickers) {
        std::cout << "  ticker: " << ticker << std::endl;
    }

    // ── Start the engine ───────────────────────────────────────────
    engine.start();
    std::cout << "MatchingEngine started. Press Enter to stop..." << std::endl;

    std::cin.get();

    // ── Graceful shutdown ──────────────────────────────────────────
    engine.stop();
    std::cout << "MatchingEngine stopped." << std::endl;

    return 0;
}
