#pragma once

/// @file fix_gateway.hpp
/// @brief FIX protocol gateway server using boost::asio.
///
/// Listens for TCP connections on a configurable address/port, spawns
/// FixSession instances for each accepted connection, and runs a
/// configurable thread pool for handling I/O. Parsed order requests
/// are pushed to a lock-free MPSC queue consumed by the MatchingEngine.

#include <atomic>
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
// #include "fix_session.hpp"
#include "lib/exchange/asset_translator.hpp"
#include "lib/utils/log.h"

namespace exchange::fix {
/// FIX Gateway — accepts TCP connections and manages FIX sessions.
///
/// Uses boost::asio with a configurable thread pool. Each accepted
/// connection creates a FixSession that translates FIX messages into
/// engine Request objects pushed to the shared MPSC queue.
class FixGateway final {
public:
    /// Constructs the gateway but does not start listening.
    ///
    /// @param config   Gateway configuration (port, threads, comp IDs).
    /// @param translator  Reference to the asset name → TickerId mapper.
    /// @param engine_queue  MPSC queue for pushing parsed Requests to the engine.
    /// @param logger   Logger instance for all gateway events.
    FixGateway(const FixGatewayConfig& config, const AssetTranslator& translator,
               utils::Logger& logger);

    ~FixGateway();

    FixGateway(const FixGateway&) = delete;
    FixGateway& operator=(const FixGateway&) = delete;

    /// Starts listening for connections and launches the I/O thread pool.
    ///
    /// @return true on success, false if already running or bind fails.
    [[nodiscard]] bool start();

    /// Stops the acceptor, drains pending work, and joins all I/O threads.
    void stop() noexcept;

    /// Returns true while the gateway is running.
    [[nodiscard]] bool isRunning() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

private:
    /// Begins an asynchronous accept operation.
    void doAccept();

    /// Handles a newly accepted connection.
    void onAccept(const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket);

    /// Work guard to keep io_context alive between async ops.
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    const FixGatewayConfig config_;
    const AssetTranslator& translator_;
    utils::Logger& logger_;

    boost::asio::io_context io_context_;
    WorkGuard work_guard_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::atomic<bool> running_{false};
    std::vector<std::thread> thread_pool_;
};

}  // namespace exchange::fix
