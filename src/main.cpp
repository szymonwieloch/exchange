#include <boost/program_options.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "lib/exchange/book.h"
#include "lib/exchange/constants.h"
#include "lib/exchange/definitions.h"
#include "lib/exchange/engine.h"
#include "lib/exchange/md.h"
#include "lib/exchange/metric_registry.h"
#include "lib/exchange/request.h"
#include "lib/main/config.h"
#include "lib/utils/die.h"
#include "lib/utils/log.h"
#include "lib/utils/mem.h"
#include "lib/utils/metrics.h"
#include "lib/utils/metrics_server.h"
#include "lib/utils/queue.h"
#include "lib/utils/thread.h"

[[nodiscard]] static inline std::unique_ptr<utils::MetricsServer> createMetricsServer(
    const Config::Metrics& metrics_cfg, const exchange::MetricRegistry& registry,
    utils::Logger& logger) {
    if (!metrics_cfg.enabled) {
        logger.info("Metrics:   disabled");
        return nullptr;
    }

    logger.info("Metrics:   enabled (port ", metrics_cfg.port, ")");

    auto callback = [&registry](utils::PrometheusFormatter& fmt) { registry.render(fmt); };

    auto server_cfg = utils::MetricsServer::Config{
        .bind_address = metrics_cfg.bind_address,
        .port = metrics_cfg.port,
    };

    auto server = std::make_unique<utils::MetricsServer>(callback, server_cfg);

    if (!server->start()) {
        logger.error("failed to start metrics server");
        return nullptr;
    }

    return server;
}

int main(int argc, char* argv[]) {
    // ── Parse command-line arguments ──────────────────────────────
    namespace po = boost::program_options;

    po::options_description desc("Options");
    desc.add_options()("help,h", "Show this help message")(
        "config,c", po::value<std::string>()->default_value("config.toml"),
        "Path to configuration file");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << "\n\n";
        std::cerr << desc << std::endl;
        return 1;
    }

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    const auto config_path = vm["config"].as<std::string>();
    std::cout << "Loading configuration from: " << config_path << std::endl;

    const auto config_result = parseConfig(config_path);
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

    utils::Logger logger(config.logging.file, config.logging.level);
    if (!logger.start()) {
        std::cerr << "could not start logger" << std::endl;
        return 1;
    }

    // Prometheus metrics
    exchange::MetricRegistry mreg;
    auto metrics_server = createMetricsServer(config.metrics, mreg, logger);

    // ── Create lock-free communication queues ──────────────────────
    auto request_queue = exchange::RequestLFQueue(exchange::MAX_USER_UPDATES);
    auto response_queue = exchange::ResponseLFQueue(exchange::MAX_USER_UPDATES);
    auto md_queue = exchange::MDLFQueue(exchange::MAX_MARKET_UPDATES);

    // ── Instantiate the matching engine ────────────────────────────
    exchange::MatchingEngine engine(&request_queue, &response_queue, &md_queue, mreg,
                                    config.logging.file, config.logging.level);

    if (config.threading.engine_core >= 0) {
        if (!utils::setThreadCore(config.threading.engine_core)) {
            logger.error("failed to pin engine thread to core ", config.threading.engine_core);
        }
    }

    // TODO: register signal handler
    engine.start();
    std::cout << "MatchingEngine started. Press Enter to stop..." << std::endl;

    std::cin.get();

    // Graceful shutdown
    if (metrics_server) {
        metrics_server->stop();
        logger.info("Metrics server stopped.");
    }
    engine.stop();
    logger.info("MatchingEngine stopped.");

    return 0;
}
