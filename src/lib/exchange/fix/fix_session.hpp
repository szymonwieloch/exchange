#pragma once

/// @file fix_session.hpp
/// @brief Per-connection FIX session handler.
///
/// Each FixSession owns a TCP socket and manages the full FIX session
/// lifecycle: connection, logon, message exchange, heartbeat, and logout.
/// Parsed order requests are pushed to the engine via an MPSC queue.
///
/// Uses fixpp for all FIX tag definitions (Fixpp::Tag::*::Id).

#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>

// Suppress deprecated warnings from fixpp (std::aligned_storage in C++23)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <fixpp/versions/v42.h>
#include <fixpp/visitor.h>
#include <fixpp/writer.h>
#pragma GCC diagnostic pop

#include "lib/exchange/asset_translator.hpp"
#include "lib/exchange/request.h"
#include "lib/utils/buffer.h"
#include "lib/utils/log.h"

namespace exchange {
class UserManager;
}

namespace exchange::fix {
class FixSessions;

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
    FixSession(SessionId id, boost::asio::ip::tcp::socket socket, const AssetTranslator& translator,
               const FixSessionConfig& config, RequestLFQueue& request_queue, utils::Logger& logger,
               UserManager& user_mgr, FixSessions& sessions)
        : id_(id),
          socket_(std::move(socket)),
          translator_(translator),
          config_(config),
          request_queue_(request_queue),
          logger_(logger),
          user_mgr_(user_mgr),
          sessions_(sessions),
          heartbeat_timer_(socket_.get_executor()) {}

    ~FixSession();

    FixSession(const FixSession&) = delete;
    FixSession& operator=(const FixSession&) = delete;

    /// Begins the asynchronous session lifecycle.
    void start();

    // --- Incoming message handlers (called by visitor) ---

    /// Handles a Logon message. Validates credentials via UserManager.
    /// @param msgView  Raw message view for extracting Username (553) and Password (554).
    void onLogon(const std::string& sender, const std::string& target, uint32_t heartbeat_secs,
                 std::string_view msgView);
    /// Handles a Heartbeat message.
    void onHeartbeat(const std::string& test_req_id);
    /// Handles a TestRequest message.
    void onTestRequest(const std::string& test_req_id);
    /// Handles a Logout message.
    void onLogout();
    /// Handles a ResendRequest (not fully supported — logs and disconnects).
    void onResendRequest(uint64_t begin_seq, uint64_t end_seq);
    /// Handles a SequenceReset message.
    void onSequenceReset(uint64_t new_seq);
    /// Handles a NewOrderSingle message.
    void onNewOrderSingle(const Fixpp::v42::Message::NewOrderSingle::Ref& order);
    /// Handles an OrderCancelRequest message.
    void onOrderCancelRequest(const Fixpp::v42::Message::OrderCancelRequest::Ref& cancel);
    /// Handles any unhandled message type.
    /// @param ref_seq_num  The MsgSeqNum from the incoming message header.
    /// @param msgView      Raw message view for extracting MsgType (35).
    void onUnhandledMessage(uint64_t ref_seq_num, std::string_view msgView);

    // --- Outgoing message builders ---

    /// Builds and sends a Logon message.
    void sendLogon();
    /// Builds and sends a Logout message with optional text.
    void sendLogout(std::string_view text = {});
    /// Builds and sends a Heartbeat message, optionally echoing a TestReqID.
    void sendHeartbeat(std::string_view test_req_id = {});
    /// Builds and sends a TestRequest message.
    void sendTestRequest();
    /// Builds and sends a Reject message (session-level, MsgType=3).
    void sendReject(uint64_t ref_seq_num, const char* text);

    /// Builds and sends a BusinessMessageReject (application-level, MsgType=j).
    /// @param ref_msg_type  The MsgType of the rejected message (e.g. "D", "F").
    /// @param reason        FIX BusinessRejectReason code.
    /// @param text          Human-readable explanation.
    void sendBusinessReject(std::string_view ref_msg_type, int reason, std::string_view text = {});

    /// Cached MsgSeqNum from the currently-parsing frame (for error reporting).
    uint64_t last_msg_seq_num_{0};

private:
    void doRead();
    void onRead(const boost::system::error_code& ec, size_t bytes_transferred);
    void doWrite();
    void onWrite(const boost::system::error_code& ec, size_t bytes_transferred);

    void resetHeartbeatTimer();
    void onHeartbeatTimeout(const boost::system::error_code& ec);

    void fail(const boost::system::error_code& ec, const char* context);

    // --- Message processing ---
    /// Processes accumulated bytes in the read buffer, extracting complete FIX
    /// messages delimited by SOH.  Returns the number of bytes consumed.
    size_t processBuffer();

    /// Dispatches a single parsed FIX message frame to the appropriate handler.
    /// On parse failure, sends a session-level Reject (MsgType=3).
    void dispatchMessage(const char* frame, size_t size);

    /// Extracts a tag value from a raw FIX frame as a string_view.
    /// Returns empty string_view if tag not found.
    [[nodiscard]] static std::string_view extractTag(const char* frame, size_t size,
                                                     std::string_view tag);

    void close() noexcept;

    // --- Header helpers ---
    /// Builds a standard header with the current outgoing sequence number.
    [[nodiscard]] Fixpp::v42::Header buildHeader() const;

    SessionId id_ = SessionId::INVALID;
    boost::asio::ip::tcp::socket socket_;
    const AssetTranslator& translator_;
    const FixSessionConfig config_;
    RequestLFQueue& request_queue_;
    utils::Logger& logger_;
    const UserManager& user_mgr_;
    FixSessions& sessions_;

    /// Timer for heartbeat / idle detection.
    boost::asio::steady_timer heartbeat_timer_;

    /// Session state.
    SessionState state_ = SessionState::Connected;
    UserId user_id_;
    bool writing_{false};

    /// Read buffer: stack-allocated, no heap in hot path.
    static constexpr size_t kReadBufferSize = 4096;
    utils::Buffer<kReadBufferSize> read_buffer_;

    /// Pending write data.
    std::string pending_write_;

    /// FIX sequence numbers.
    uint64_t seq_num_in_{1};   ///< Expected incoming sequence number.
    uint64_t seq_num_out_{1};  ///< Next outgoing sequence number.
};

}  // namespace exchange::fix
