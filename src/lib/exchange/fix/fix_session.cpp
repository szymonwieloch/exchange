/// @file fix_session.cpp
/// @brief Implementation of the per-connection FIX session handler.
///
/// Parses incoming FIX messages using fixpp tag types (Fixpp::Tag::*::Id)
/// via the fixpp visitor.  Builds outgoing messages with Fixpp::Writer
/// (stack-allocated stream buffer, 1KB stack + dynamic overflow).

#include "fix_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>

#include "lib/exchange/definitions.h"
#include "lib/utils/log.h"

namespace exchange::fix {

namespace {

[[nodiscard]] constexpr Side toInternalSide(char c) noexcept {
    switch (c) {
        case '1':
            return Side::BUY;
        case '2':
            return Side::SELL;
        default:
            return Side::INVALID;
    }
}

[[nodiscard]] constexpr RequestType toRequestType(char c) noexcept {
    switch (c) {
        case '1':
            return RequestType::NEW;
        case '2':
            return RequestType::NEW;
        default:
            return RequestType::INVALID;
    }
}

struct SessionVisitor : public Fixpp::StaticVisitor<void> {
    FixSession& session;
    explicit SessionVisitor(FixSession& s) : session(s) {}

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
        session.onLogon(sender, target, static_cast<uint32_t>(heartbeat));
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
    void operator()(H /*header*/, M /*msg*/) {
        session.onUnhandledMessage();
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

[[nodiscard]] const char* findBytes(const char* haystack, size_t haystack_len, const char* needle,
                                    size_t needle_len) noexcept {
    if (needle_len == 0)
        return haystack;
    if (haystack_len < needle_len)
        return nullptr;
    const char* const end = haystack + haystack_len - needle_len;
    for (const char* p = haystack; p <= end; ++p) {
        if (std::memcmp(p, needle, needle_len) == 0)
            return p;
    }
    return nullptr;
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

void FixSession::doRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_.data() + read_buffer_pos_,
                            read_buffer_.size() - read_buffer_pos_),
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
    const size_t total_bytes = read_buffer_pos_ + bytes_transferred;
    const size_t consumed = processBuffer(total_bytes);
    if (consumed < total_bytes) {
        const size_t remaining = total_bytes - consumed;
        std::memmove(read_buffer_.data(), read_buffer_.data() + consumed, remaining);
        read_buffer_pos_ = remaining;
    } else {
        read_buffer_pos_ = 0;
    }
    doRead();
}

size_t FixSession::processBuffer(size_t total_bytes) {
    const char* data = read_buffer_.data();
    size_t consumed = 0;
    while (consumed < total_bytes) {
        const char* start = data + consumed;
        const char* const end = data + total_bytes;
        const char* cs = findBytes(start, static_cast<size_t>(end - start), "10=", 3);
        if (cs == nullptr)
            break;
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
    SessionVisitor visitor(*this);
    auto result = Fixpp::visit(frame, size, visitor, SessionVisitRules{});
    if (!result.isOk()) {
        const auto& err = result.unwrapErr();
        logger_.warn("FIX parse error: ", utils::ShortString(err.asString()), " at column ",
                     err.column());
    }
}

void FixSession::onLogon(const std::string& /*sender*/, const std::string& /*target*/,
                         uint32_t heartbeat_secs) {
    if (heartbeat_secs > 0) {
        const_cast<FixSessionConfig&>(config_).heartbeat_interval =
            std::min(heartbeat_secs, config_.heartbeat_interval);
    }
    state_ = SessionState::LoggedOn;
    logger_.info("FIX session logged on (heartbeat=", config_.heartbeat_interval, "s)");
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
    boost::system::error_code ec;
    heartbeat_timer_.cancel(ec);
    socket_.close(ec);
    state_ = SessionState::Disconnected;
}

void FixSession::onResendRequest(uint64_t /*begin_seq*/, uint64_t /*end_seq*/) {
    logger_.warn("FIX ResendRequest received (not supported), disconnecting");
    sendLogout("ResendRequest not supported");
    boost::system::error_code ec;
    heartbeat_timer_.cancel(ec);
    socket_.close(ec);
    state_ = SessionState::Disconnected;
}

void FixSession::onSequenceReset(uint64_t new_seq) {
    logger_.info("FIX SequenceReset to ", new_seq);
    seq_num_in_ = new_seq;
}

void FixSession::onUnhandledMessage() {
    logger_.warn("FIX unhandled message type received");
}

void FixSession::onNewOrderSingle(const Fixpp::v42::Message::NewOrderSingle::Ref& order) {
    if (state_ != SessionState::LoggedOn) {
        logger_.warn("FIX NewOrderSingle received before logon, ignoring");
        return;
    }
    std::string symbol;
    if (!Fixpp::tryGet<Fixpp::Tag::Symbol>(order, symbol)) {
        logger_.warn("FIX NewOrderSingle missing Symbol");
        return;
    }
    const TickerId ticker = translator_.resolve(symbol);
    if (ticker == TickerId::INVALID) {
        logger_.warn("FIX NewOrderSingle unknown symbol: ", utils::ShortString(symbol));
        return;
    }
    char side_char = 0;
    if (!Fixpp::tryGet<Fixpp::Tag::Side>(order, side_char)) {
        logger_.warn("FIX NewOrderSingle missing Side");
        return;
    }
    const Side side = toInternalSide(side_char);
    if (side == Side::INVALID) {
        logger_.warn("FIX NewOrderSingle invalid Side: ", side_char);
        return;
    }
    char ord_type = 0;
    if (!Fixpp::tryGet<Fixpp::Tag::OrdType>(order, ord_type)) {
        logger_.warn("FIX NewOrderSingle missing OrdType");
        return;
    }
    const RequestType req_type = toRequestType(ord_type);
    if (req_type == RequestType::INVALID) {
        logger_.warn("FIX NewOrderSingle unsupported OrdType: ", ord_type);
        return;
    }
    double price_val = 0.0;
    const bool has_price = Fixpp::tryGet<Fixpp::Tag::Price>(order, price_val);
    double qty_val = 0.0;
    if (!Fixpp::tryGet<Fixpp::Tag::OrderQty>(order, qty_val)) {
        logger_.warn("FIX NewOrderSingle missing OrderQty");
        return;
    }
    std::string cl_ord_id;
    Fixpp::tryGet<Fixpp::Tag::ClOrdID>(order, cl_ord_id);
    UserId user_id = UserId::INVALID;
    if (!cl_ord_id.empty()) {
        uint32_t hash = 0;
        for (char c : cl_ord_id)
            hash = hash * 31 + static_cast<uint32_t>(static_cast<unsigned char>(c));
        user_id = UserId{hash % 1024 + 1};
    }
    Request req{
        .type = req_type,
        .user_id = user_id,
        .ticker_id = ticker,
        .order_id = OrderId::INVALID,
        .side = side,
        .price = has_price ? Price{static_cast<uint64_t>(price_val * 100)} : Price::INVALID,
        .qty = Quantity{static_cast<uint32_t>(qty_val)},
    };
    const size_t idx = request_queue_.reserve(1);
    if (idx == static_cast<size_t>(-1)) {
        logger_.warn("FIX request queue full, dropping NewOrderSingle");
        return;
    }
    *request_queue_.slot(idx) = req;
    request_queue_.commit(idx, 1);
    logger_.debug("FIX NewOrderSingle: ticker=", static_cast<int>(type_safe::get(ticker)),
                  " side=", static_cast<int>(side), " qty=", qty_val, " price=", price_val);
}

void FixSession::onOrderCancelRequest(const Fixpp::v42::Message::OrderCancelRequest::Ref& cancel) {
    if (state_ != SessionState::LoggedOn) {
        logger_.warn("FIX OrderCancelRequest received before logon, ignoring");
        return;
    }
    std::string symbol;
    if (!Fixpp::tryGet<Fixpp::Tag::Symbol>(cancel, symbol)) {
        logger_.warn("FIX OrderCancelRequest missing Symbol");
        return;
    }
    const TickerId ticker = translator_.resolve(symbol);
    if (ticker == TickerId::INVALID) {
        logger_.warn("FIX OrderCancelRequest unknown symbol: ", utils::ShortString(symbol));
        return;
    }
    char side_char = 0;
    if (!Fixpp::tryGet<Fixpp::Tag::Side>(cancel, side_char)) {
        logger_.warn("FIX OrderCancelRequest missing Side");
        return;
    }
    const Side side = toInternalSide(side_char);
    if (side == Side::INVALID) {
        logger_.warn("FIX OrderCancelRequest invalid Side: ", side_char);
        return;
    }
    std::string cl_ord_id;
    Fixpp::tryGet<Fixpp::Tag::ClOrdID>(cancel, cl_ord_id);
    uint32_t hash = 0;
    for (char c : cl_ord_id)
        hash = hash * 31 + static_cast<uint32_t>(static_cast<unsigned char>(c));
    const UserId user_id{hash % 1024 + 1};
    Request req{
        .type = RequestType::CANCEL,
        .user_id = user_id,
        .ticker_id = ticker,
        .order_id = OrderId::INVALID,
        .side = side,
        .price = Price::INVALID,
        .qty = Quantity::INVALID,
    };
    const size_t idx = request_queue_.reserve(1);
    if (idx == static_cast<size_t>(-1)) {
        logger_.warn("FIX request queue full, dropping OrderCancelRequest");
        return;
    }
    *request_queue_.slot(idx) = req;
    request_queue_.commit(idx, 1);
    logger_.debug("FIX OrderCancelRequest: ticker=", static_cast<int>(type_safe::get(ticker)),
                  " side=", static_cast<int>(side));
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
        boost::system::error_code ignored;
        socket_.close(ignored);
        state_ = SessionState::Disconnected;
    }
}

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
