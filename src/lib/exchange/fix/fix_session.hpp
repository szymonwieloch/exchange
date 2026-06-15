#pragma once

/// @file fix_session.hpp
/// @brief Per-connection FIX session handler.
///
/// Each FixSession owns a TCP socket and manages the full FIX session
/// lifecycle: connection, logon, message exchange, heartbeat, and logout.
/// Parsed order requests are pushed to the engine via an MPSC queue.
///
/// Uses fixpp for all FIX tag definitions (Fixpp::Tag::*::Id).

#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include "fix_messages.hpp"
#include "lib/exchange/asset_translator.hpp"
#include "lib/exchange/request.h"
#include "lib/utils/log.h"
#include "lib/utils/queue.h"
#include "message_types.hpp"

namespace exchange::fix {

/// Configuration for a single FIX session acceptor.
struct FixSessionConfig {
    std::string sender_comp_id;
    std::string target_comp_id;
    uint32_t heartbeat_interval;
};

enum class SessionState : uint8_t {
    Disconnected,
    Connected,
    LoggedOn,
};

/// A single client FIX session bound to one TCP socket.
///
/// Lifetime is managed via `std::enable_shared_from_this` — the session
/// keeps itself alive while async operations are pending.
class FixSession final : public std::enable_shared_from_this<FixSession> {
public:
    FixSession(boost::asio::ip::tcp::socket socket, const AssetTranslator& translator,
               const FixSessionConfig& config, utils::Logger& logger)
        : socket_(std::move(socket)),
          translator_(translator),
          config_(config),
          logger_(logger),
          heartbeat_timer_(socket_.get_executor()) {}

    ~FixSession();

    FixSession(const FixSession&) = delete;
    FixSession& operator=(const FixSession&) = delete;

    /// Begins the asynchronous session lifecycle.
    void start();

private:
    void doRead();
    void onRead(const boost::system::error_code& ec, size_t bytes_transferred);
    void doWrite();
    void onWrite(const boost::system::error_code& ec, size_t bytes_transferred);

    void resetHeartbeatTimer();
    void onHeartbeatTimeout(const boost::system::error_code& ec);

    void fail(const boost::system::error_code& ec, const char* context);

    boost::asio::ip::tcp::socket socket_;
    const AssetTranslator& translator_;
    const FixSessionConfig config_;
    utils::Logger& logger_;

    /// Timer for heartbeat / idle detection.
    boost::asio::steady_timer heartbeat_timer_;

    /// Session state.
    SessionState state_ = SessionState::Connected;
    bool writing_{false};
};

}  // namespace exchange::fix
