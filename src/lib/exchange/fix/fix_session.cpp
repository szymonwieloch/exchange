/// @file fix_session.cpp
/// @brief Implementation of the per-connection FIX session handler.
///
/// Parses incoming FIX messages using fixpp tag types (Fixpp::Tag::*::Id)
/// via the fixpp visitor.  Builds outgoing messages with Fixpp::Writer
/// (stack-allocated stream buffer, 1KB stack + dynamic overflow).

#include "fix_session.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <ctime>

#include "lib/exchange/definitions.h"
#include "lib/exchange/fix/fix_helpers.hpp"
#include "lib/exchange/user_mgr.h"
#include "lib/utils/log.h"

namespace exchange::fix {

namespace {

struct SessionVisitor : public Fixpp::StaticVisitor<void> {
    FixSession& session;
    std::string_view msgView;
    SessionVisitor(FixSession& s, std::string_view mv) : session(s), msgView(mv) {}

    void operator()(const Fixpp::v42::Header::Ref& header,
                    const Fixpp::v42::Message::Logon::Ref& logon) {
        std::string sender;
        if (!Fixpp::tryGet<Fixpp::Tag::SenderCompID>(header, sender)) {
            session.sendReject(0, "Missing SenderCompID");
            return;
        }
        std::string target;
        if (!Fixpp::tryGet<Fixpp::Tag::TargetCompID>(header, target)) {
            session.sendReject(0, "Missing TargetCompID");
            return;
        }
        int64_t heartbeat = 0;
        Fixpp::tryGet<Fixpp::Tag::HeartBtInt>(logon, heartbeat);
        session.onLogon(sender, target, static_cast<uint32_t>(heartbeat), msgView);
    }

    void operator()(const Fixpp::v42::Header::Ref& /*header*/,
                    const Fixpp::v42::Message::Heartbeat::Ref& heartbeat) {
        std::string test_req_id;
        Fixpp::tryGet<Fixpp::Tag::TestReqID>(heartbeat, test_req_id);
        session.onHeartbeat(test_req_id);
    }

    void operator()(const Fixpp::v42::Header::Ref& /*header*/,
                    const Fixpp::v42::Message::TestRequest::Ref& test_req) {
        std::string test_req_id;
        Fixpp::tryGet<Fixpp::Tag::TestReqID>(test_req, test_req_id);
        session.onTestRequest(test_req_id);
    }

    void operator()(const Fixpp::v42::Header::Ref& /*header*/,
                    const Fixpp::v42::Message::Logout::Ref& /*logout*/) {
        session.onLogout();
    }

    void operator()(const Fixpp::v42::Header::Ref& /*header*/,
                    const Fixpp::v42::Message::ResendRequest::Ref& resend) {
        int64_t begin_seq = 0, end_seq = 0;
        Fixpp::tryGet<Fixpp::Tag::BeginSeqNo>(resend, begin_seq);
        Fixpp::tryGet<Fixpp::Tag::EndSeqNo>(resend, end_seq);
        session.onResendRequest(static_cast<uint64_t>(begin_seq), static_cast<uint64_t>(end_seq));
    }

    void operator()(const Fixpp::v42::Header::Ref& /*header*/,
                    const Fixpp::v42::Message::SequenceReset::Ref& seq_reset) {
        int64_t new_seq = 0;
        Fixpp::tryGet<Fixpp::Tag::NewSeqNo>(seq_reset, new_seq);
        session.onSequenceReset(static_cast<uint64_t>(new_seq));
    }

    void operator()(const Fixpp::v42::Header::Ref& /*header*/,
                    const Fixpp::v42::Message::NewOrderSingle::Ref& order) {
        session.onNewOrderSingle(order);
    }

    void operator()(const Fixpp::v42::Header::Ref& /*header*/,
                    const Fixpp::v42::Message::OrderCancelRequest::Ref& cancel) {
        session.onOrderCancelRequest(cancel);
    }

    template <typename H, typename M>
    void operator()(H header, M /*msg*/) {
        int64_t seq_num = 0;
        Fixpp::tryGet<Fixpp::Tag::MsgSeqNum>(header, seq_num);
        session.onUnhandledMessage(static_cast<uint64_t>(seq_num), msgView);
    }
};

struct SessionVisitRules : public Fixpp::VisitRules {
    using Overrides = OverrideSet<>;
    using Dictionary = Fixpp::v42::Spec::Dictionary;
    static constexpr bool ValidateChecksum = true;
    static constexpr bool ValidateLength = true;
    static constexpr bool StrictMode = false;
    static constexpr bool SkipUnknownTags = false;
};

[[nodiscard]] Fixpp::Type::UTCTimestamp::Time makeSendingTime() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    const auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000000;
    return {tt, {}, static_cast<int>(us.count())};
}

template <typename Message>
void serializeAndSend(const Fixpp::v42::Header& header, const Message& msg, uint64_t& seq_num,
                      std::string& pending_out) {
    Fixpp::Writer writer;
    pending_out = writer.write(header, msg);
    ++seq_num;
}

}  // namespace

FixSession::~FixSession() {
    close();
}

void FixSession::start() {
    logger_.info("FIX session connected from ",
                 utils::ShortString(socket_.remote_endpoint().address().to_string()), ":",
                 socket_.remote_endpoint().port());
    resetHeartbeatTimer();
    doRead();
}

void FixSession::doRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_.writeBuf(), read_buffer_.writeSize()),
        [this, self](const boost::system::error_code& ec, size_t bytes_transferred) {
            onRead(ec, bytes_transferred);
        });
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
    read_buffer_.extend(bytes_transferred);
    const size_t consumed = processBuffer();
    read_buffer_.consume(consumed);
    doRead();
}

size_t FixSession::processBuffer() {
    auto total_bytes = read_buffer_.size();
    const char* data = read_buffer_.data();
    size_t consumed = 0;
    auto end = read_buffer_.end();
    while (consumed < total_bytes) {
        const char* start = data + consumed;
        std::string_view segment(start, static_cast<size_t>(end - start));
        auto cs_pos = segment.find("10=");
        if (cs_pos == std::string_view::npos)
            break;
        const char* cs = start + cs_pos;
        const char* soh = static_cast<const char*>(
            std::memchr(cs + 3, Fixpp::SOH, static_cast<size_t>(end - (cs + 3))));
        if (soh == nullptr)
            break;
        const size_t msg_len = static_cast<size_t>(soh - start) + 1;
        dispatchMessage(start, msg_len);
        consumed += msg_len;
    }
    return consumed;
}

void FixSession::dispatchMessage(const char* frame, size_t size) {
    // Cache MsgSeqNum from the raw frame before parsing,
    // so error handlers can reference it.
    last_msg_seq_num_ = 0;
    {
        auto sn = extractTag(frame, size, "34=");
        if (!sn.empty()) {
            auto fc = std::from_chars(sn.data(), sn.data() + sn.size(), last_msg_seq_num_);
            if (fc.ec != std::errc{})
                last_msg_seq_num_ = 0;
        }
    }

    const std::string_view msgView(frame, size);
    SessionVisitor visitor(*this, msgView);
    auto result = Fixpp::visit(frame, size, visitor, SessionVisitRules{});
    if (!result.isOk()) {
        const auto& err = result.unwrapErr();
        logger_.warn("FIX parse error: ", utils::ShortString(err.asString()), " at column ",
                     err.column());
        sendReject(last_msg_seq_num_, err.asString().c_str());
    }
}

std::string_view FixSession::extractTag(const char* frame, size_t size, std::string_view tag) {
    std::string_view sv(frame, size);
    auto pos = sv.find(tag);
    if (pos == std::string_view::npos)
        return {};
    const char* val_start = frame + pos + tag.size();
    const char* soh = static_cast<const char*>(
        std::memchr(val_start, Fixpp::SOH, size - static_cast<size_t>(val_start - frame)));
    if (soh == nullptr)
        return {};
    return {val_start, static_cast<size_t>(soh - val_start)};
}

void FixSession::onLogon(const std::string& /*sender*/, const std::string& /*target*/,
                         uint32_t heartbeat_secs, std::string_view msgView) {
    // Extract credentials from the raw message view
    std::string username{extractTag(msgView.data(), msgView.size(), "553=")};
    std::string password{extractTag(msgView.data(), msgView.size(), "554=")};

    // Validate credentials
    auto opt_user = user_mgr_.checkUser(username, password);
    if (!opt_user.has_value()) {
        logger_.warn("FIX logon rejected: invalid credentials for '", utils::ShortString(username),
                     "'");
        sendLogout("Invalid username or password");
        close();
        return;
    }
    user_id_ = *opt_user;

    if (heartbeat_secs > 0) {
        const_cast<FixSessionConfig&>(config_).heartbeat_interval =
            std::min(heartbeat_secs, config_.heartbeat_interval);
    }
    state_ = SessionState::LoggedOn;
    logger_.info("FIX session logged on (user=", type_safe::get(user_id_),
                 " heartbeat=", config_.heartbeat_interval, "s)");
    sendLogon();
    resetHeartbeatTimer();
}

void FixSession::onHeartbeat(const std::string& /*test_req_id*/) {
    logger_.debug("FIX Heartbeat received");
    resetHeartbeatTimer();
}

void FixSession::onTestRequest(const std::string& test_req_id) {
    logger_.debug("FIX TestRequest received, echoing heartbeat");
    sendHeartbeat(test_req_id);
    resetHeartbeatTimer();
}

void FixSession::onLogout() {
    logger_.info("FIX Logout received");
    sendLogout();
    close();
}

void FixSession::onResendRequest(uint64_t /*begin_seq*/, uint64_t /*end_seq*/) {
    logger_.warn("FIX ResendRequest received (not supported), disconnecting");
    sendLogout("ResendRequest not supported");
    close();
}

void FixSession::onSequenceReset(uint64_t new_seq) {
    logger_.info("FIX SequenceReset to ", new_seq);
    seq_num_in_ = new_seq;
}

void FixSession::onUnhandledMessage(uint64_t /*ref_seq_num*/, std::string_view msgView) {
    auto mt = extractTag(msgView.data(), msgView.size(), "35=");
    std::string_view msgType = mt.empty() ? std::string_view{"?"} : mt;
    std::string mts{msgType};
    logger_.warn("FIX unhandled message type: ", utils::ShortString(mts));
    // BusinessRejectReason 3 = Unsupported Message Type
    sendBusinessReject(msgType, 3, "Unsupported message type");
}

void FixSession::onNewOrderSingle(const Fixpp::v42::Message::NewOrderSingle::Ref& order) {
    if (state_ != SessionState::LoggedOn) {
        logger_.warn("FIX NewOrderSingle received before logon, ignoring");
        return;
    }

    // --- Parse and validate ---

    auto result = details::parseNewOrderSingle(order, user_id_, translator_);
    if (!result.has_value()) {
        logger_.warn("FIX NewOrderSingle: ", result.error());
        // Determine BusinessRejectReason: 5 = Required tag missing, 0 = Other
        const bool is_missing = (std::string_view{result.error()}.find("Required tag missing") !=
                                 std::string_view::npos);
        sendBusinessReject("D", is_missing ? 5 : 0, result.error());
        return;
    }

    // --- Enqueue request ---

    const size_t idx = request_queue_.reserve(1);
    if (idx == static_cast<size_t>(-1)) {
        logger_.warn("FIX request queue full, dropping NewOrderSingle");
        // BusinessRejectReason 4 = Application not available
        sendBusinessReject("D", 4, "System busy, please retry");
        return;
    }
    const Request& req = *result;
    *request_queue_.slot(idx) = req;
    request_queue_.commit(idx, 1);

    logger_.debug("FIX NewOrderSingle: ticker=", static_cast<int>(type_safe::get(req.ticker_id)),
                  " side=", static_cast<int>(req.side), " qty=", type_safe::get(req.qty),
                  " price=", type_safe::get(req.price));
}

void FixSession::onOrderCancelRequest(const Fixpp::v42::Message::OrderCancelRequest::Ref& cancel) {
    if (state_ != SessionState::LoggedOn) {
        logger_.warn("FIX OrderCancelRequest received before logon, ignoring");
        return;
    }

    // --- Parse and validate ---

    auto result = details::parseOrderCancelRequest(cancel, user_id_, translator_);
    if (!result.has_value()) {
        logger_.warn("FIX OrderCancelRequest: ", result.error());
        const bool is_missing = (std::string_view{result.error()}.find("Required tag missing") !=
                                 std::string_view::npos);
        sendBusinessReject("F", is_missing ? 5 : 0, result.error());
        return;
    }

    // --- Enqueue request ---

    const size_t idx = request_queue_.reserve(1);
    if (idx == static_cast<size_t>(-1)) {
        logger_.warn("FIX request queue full, dropping OrderCancelRequest");
        sendBusinessReject("F", 4, "System busy, please retry");
        return;
    }
    const Request& req = *result;
    *request_queue_.slot(idx) = req;
    request_queue_.commit(idx, 1);

    logger_.debug(
        "FIX OrderCancelRequest: ticker=", static_cast<int>(type_safe::get(req.ticker_id)),
        " side=", static_cast<int>(req.side));
}

Fixpp::v42::Header FixSession::buildHeader() const {
    Fixpp::v42::Header header;
    Fixpp::set<Fixpp::Tag::SenderCompID>(header, config_.sender_comp_id);
    Fixpp::set<Fixpp::Tag::TargetCompID>(header, config_.target_comp_id);
    Fixpp::set<Fixpp::Tag::MsgSeqNum>(header, static_cast<int64_t>(seq_num_out_));
    Fixpp::set<Fixpp::Tag::SendingTime>(header, makeSendingTime());
    return header;
}

void FixSession::sendLogon() {
    Fixpp::v42::Message::Logon logon;
    Fixpp::set<Fixpp::Tag::EncryptMethod>(logon, 0);
    Fixpp::set<Fixpp::Tag::HeartBtInt>(logon, static_cast<int64_t>(config_.heartbeat_interval));
    auto header = buildHeader();
    serializeAndSend(header, logon, seq_num_out_, pending_write_);
    doWrite();
    logger_.debug("FIX Logon sent");
}

void FixSession::sendLogout(std::string_view text) {
    Fixpp::v42::Message::Logout logout;
    if (!text.empty()) {
        Fixpp::set<Fixpp::Tag::Text>(logout, std::string(text));
    }
    auto header = buildHeader();
    serializeAndSend(header, logout, seq_num_out_, pending_write_);
    doWrite();
    logger_.debug("FIX Logout sent");
}

void FixSession::sendHeartbeat(std::string_view test_req_id) {
    Fixpp::v42::Message::Heartbeat hb;
    if (!test_req_id.empty()) {
        Fixpp::set<Fixpp::Tag::TestReqID>(hb, std::string(test_req_id));
    }
    auto header = buildHeader();
    serializeAndSend(header, hb, seq_num_out_, pending_write_);
    doWrite();
}

void FixSession::sendTestRequest() {
    Fixpp::v42::Message::TestRequest test_req;
    Fixpp::set<Fixpp::Tag::TestReqID>(test_req, "HEARTBEAT");
    auto header = buildHeader();
    serializeAndSend(header, test_req, seq_num_out_, pending_write_);
    doWrite();
    logger_.debug("FIX TestRequest sent");
}

void FixSession::sendReject(uint64_t ref_seq_num, const char* text) {
    Fixpp::v42::Message::Reject reject;
    Fixpp::set<Fixpp::Tag::RefSeqNum>(reject, static_cast<int64_t>(ref_seq_num));
    if (text != nullptr) {
        Fixpp::set<Fixpp::Tag::Text>(reject, std::string(text));
    }
    auto header = buildHeader();
    serializeAndSend(header, reject, seq_num_out_, pending_write_);
    doWrite();
    logger_.debug("FIX Reject sent (ref=", ref_seq_num, ")");
}

void FixSession::sendBusinessReject(std::string_view ref_msg_type, int reason,
                                    std::string_view text) {
    Fixpp::v42::Message::BusinessMessageReject bmr;
    Fixpp::set<Fixpp::Tag::RefMsgType>(bmr, std::string(ref_msg_type));
    Fixpp::set<Fixpp::Tag::BusinessRejectReason>(bmr, static_cast<int64_t>(reason));
    if (!text.empty()) {
        Fixpp::set<Fixpp::Tag::Text>(bmr, std::string(text));
    }
    auto header = buildHeader();
    serializeAndSend(header, bmr, seq_num_out_, pending_write_);
    doWrite();
    std::string rt{ref_msg_type};
    logger_.debug("FIX BusinessMessageReject sent (refType=", utils::ShortString(rt),
                  " reason=", reason, ")");
}

void FixSession::doWrite() {
    if (writing_ || pending_write_.empty())
        return;
    writing_ = true;
    auto self = shared_from_this();
    boost::asio::async_write(
        socket_, boost::asio::buffer(pending_write_.data(), pending_write_.size()),
        [this, self](const boost::system::error_code& ec, size_t bytes_transferred) {
            onWrite(ec, bytes_transferred);
        });
}

void FixSession::onWrite(const boost::system::error_code& ec, size_t /*bytes_transferred*/) {
    writing_ = false;
    if (ec) {
        fail(ec, "write");
        return;
    }
    pending_write_.clear();
    if (!pending_write_.empty())
        doWrite();
}

void FixSession::resetHeartbeatTimer() {
    heartbeat_timer_.cancel();
    heartbeat_timer_.expires_after(std::chrono::seconds(config_.heartbeat_interval));
    auto self = shared_from_this();
    heartbeat_timer_.async_wait(
        [this, self](const boost::system::error_code& ec) { onHeartbeatTimeout(ec); });
}

void FixSession::onHeartbeatTimeout(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec) {
        fail(ec, "heartbeat timer");
        return;
    }
    if (state_ == SessionState::LoggedOn) {
        sendTestRequest();
        resetHeartbeatTimer();
    } else {
        logger_.info("FIX session timeout (no logon received)");
        close();
    }
}

void FixSession::fail(const boost::system::error_code& ec, const char* context) {
    if (ec == boost::asio::error::operation_aborted)
        return;
    logger_.error("FIX session error (", context, "): ", utils::ShortString(ec.message()));
    close();
}

}  // namespace exchange::fix
