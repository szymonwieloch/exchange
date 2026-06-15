/// @file fix_session.cpp
/// @brief Implementation of the per-connection FIX session handler.
///
/// Parses incoming FIX messages using fixpp tag types (Fixpp::Tag::*::Id)
/// via FixParser.  Builds outgoing messages with FixWriteBuffer (raw
/// tag=value<SOH> format, stack-allocated, no heap allocations).

#include "fix_session.hpp"

#include <charconv>
#include <chrono>
#include <cstring>
#include <ctime>

#include "lib/exchange/definitions.h"
#include "lib/utils/log.h"

namespace exchange::fix {

// ═════════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═════════════════════════════════════════════════════════════════════════

FixSession::~FixSession() {
    boost::system::error_code ec;
    heartbeat_timer_.cancel(ec);
    socket_.close(ec);
}

void FixSession::start() {
    logger_.info("FIX session connected from ",
                 utils::ShortString(socket_.remote_endpoint().address().to_string()), ":",
                 socket_.remote_endpoint().port());
    resetHeartbeatTimer();
    doRead();
}

// ═════════════════════════════════════════════════════════════════════════
//  Async Read
// ═════════════════════════════════════════════════════════════════════════

void FixSession::doRead() {
    auto self = shared_from_this();
    // socket_.async_read_some(
    //     boost::asio::buffer(read_buffer_.data() + read_buffer_pos_,
    //                         read_buffer_.size() - read_buffer_pos_),
    //     [this, self](const boost::system::error_code& ec, size_t bytes_transferred) {
    //         onRead(ec, bytes_transferred);
    //     });

    // TODO
}

void FixSession::onRead(const boost::system::error_code& ec, size_t bytes_transferred) {
    if (ec) {
        if (ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset) {
            logger_.info("FIX session disconnected: ", utils::ShortString(ec.message()));
        } else {
            fail(ec, "read");
        }
        return;
    }
    (void)bytes_transferred;
    // TODO

    doRead();
}

// ═════════════════════════════════════════════════════════════════════════
//  Async Write
// ═════════════════════════════════════════════════════════════════════════

void FixSession::doWrite() {
    if (writing_) {
        return;
    }

    // TODO

    // auto self = shared_from_this();
    // boost::asio::async_write(
    //     socket_, boost::asio::buffer(pending_buf_.data.data(), pending_buf_.len),
    //     [this, self](const boost::system::error_code& ec, size_t bt) { onWrite(ec, bt); });
}

void FixSession::onWrite(const boost::system::error_code& ec, size_t /*bytes_transferred*/) {
    writing_ = false;

    if (ec) {
        fail(ec, "write");
        return;
    }

    // if (write_buf_.len > 0) {
    //     doWrite();
    // }

    // TODO
}

// ═════════════════════════════════════════════════════════════════════════
//  Heartbeat Timer
// ═════════════════════════════════════════════════════════════════════════

void FixSession::resetHeartbeatTimer() {
    heartbeat_timer_.cancel();
    heartbeat_timer_.expires_after(std::chrono::seconds(config_.heartbeat_interval));

    auto self = shared_from_this();
    heartbeat_timer_.async_wait(
        [this, self](const boost::system::error_code& ec) { onHeartbeatTimeout(ec); });
}

void FixSession::onHeartbeatTimeout(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::operation_aborted) {
        return;
    }
    if (ec) {
        fail(ec, "heartbeat timer");
        return;
    }

    if (state_ == SessionState::LoggedOn) {
        // TODO

        logger_.debug("FIX TestRequest sent (heartbeat timeout)");
        resetHeartbeatTimer();
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  Error Handling
// ═════════════════════════════════════════════════════════════════════════

void FixSession::fail(const boost::system::error_code& ec, const char* context) {
    if (ec == boost::asio::error::operation_aborted)
        return;

    logger_.error("FIX session error (", context, "): ", utils::ShortString(ec.message()));

    boost::system::error_code ignored;
    heartbeat_timer_.cancel(ignored);
    socket_.close(ignored);
    state_ = SessionState::Disconnected;
}

}  // namespace exchange::fix
