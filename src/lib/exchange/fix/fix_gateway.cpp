/// @file fix_gateway.cpp
/// @brief Implementation of the FIX gateway server.

#include "fix_gateway.hpp"

#include <functional>

#include "fix_session.hpp"
#include "lib/utils/log.h"

namespace exchange::fix {

FixGateway::FixGateway(const FixGatewayConfig& config, const AssetTranslator& translator,
                       RequestLFQueue& request_queue, ResponseLFQueue& response_queue,
                       UserManager& user_mgr, utils::Logger& logger)
    : config_(config),
      translator_(translator),
      logger_(logger),
      request_queue_(request_queue),
      response_queue_(response_queue),
      user_mgr_(user_mgr),
      work_guard_(boost::asio::make_work_guard(io_context_)),
      acceptor_(io_context_),
      response_thread_("fix-resp", logger, config.response_thread_core),
      sessions_(logger, translator, request_queue, user_mgr,
                FixSessionConfig{.sender_comp_id = config.sender_comp_id,
                                 .target_comp_id = config.target_comp_id,
                                 .heartbeat_interval = config.heartbeat_interval}) {}

FixGateway::~FixGateway() {
    stop();
}

bool FixGateway::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        logger_.warn("FIX gateway already running");
        return false;
    }

    boost::system::error_code ec;

    // Resolve the bind address
    boost::asio::ip::tcp::resolver resolver(io_context_);
    const auto endpoints = resolver.resolve(config_.bind_address, std::to_string(config_.port), ec);
    if (ec) {
        logger_.error("FIX gateway: failed to resolve ", utils::ShortString::shorten(config_.bind_address),
                      ":", config_.port, " — ", utils::ShortString::shorten(ec.message()));
        running_ = false;
        return false;
    }

    // Open and bind the acceptor
    const auto& endpoint = *endpoints.begin();
    acceptor_.open(boost::asio::ip::tcp::v4(), ec);
    if (ec) {
        logger_.error("FIX gateway: failed to open acceptor — ", utils::ShortString::shorten(ec.message()));
        running_ = false;
        return false;
    }

    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    acceptor_.bind(endpoint, ec);
    if (ec) {
        logger_.error("FIX gateway: failed to bind to ", utils::ShortString::shorten(config_.bind_address),
                      ":", config_.port, " — ", utils::ShortString::shorten(ec.message()));
        running_ = false;
        return false;
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        logger_.error("FIX gateway: failed to listen — ", utils::ShortString::shorten(ec.message()));
        running_ = false;
        return false;
    }

    logger_.info("FIX gateway listening on ", utils::ShortString::shorten(config_.bind_address), ":",
                 config_.port, " (sender=", utils::ShortString::shorten(config_.sender_comp_id),
                 ", target=", utils::ShortString::shorten(config_.target_comp_id), ")");

    // Launch the thread pool
    const auto num_threads = (config_.num_threads > 0)
                                 ? config_.num_threads
                                 : static_cast<uint32_t>(std::thread::hardware_concurrency());
    logger_.info("FIX gateway starting ", static_cast<int>(num_threads), " I/O threads");

    thread_pool_.reserve(num_threads);
    for (uint32_t i = 0; i < num_threads; ++i) {
        thread_pool_.emplace_back([this, i]() {
            (void)utils::setThreadName(("fix-io-" + std::to_string(i)).c_str());
            logger_.debug("FIX I/O thread ", i, " started");
            io_context_.run();
            logger_.debug("FIX I/O thread ", i, " stopped");
        });
    }

    // Launch the response dispatch thread
    [[maybe_unused]] const bool started = response_thread_.start(
        [this](const std::atomic<bool>& is_running) { processResponses(is_running); });

    // Begin accepting connections
    doAccept();

    return true;
}

void FixGateway::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;  // already stopped
    }

    logger_.info("FIX gateway stopping...");

    boost::system::error_code ec;
    acceptor_.close(ec);

    // Release the work guard so io_context::run() can exit when drained
    work_guard_.reset();
    io_context_.stop();

    for (auto& t : thread_pool_) {
        if (t.joinable()) {
            t.join();
        }
    }
    thread_pool_.clear();

    response_thread_.stop();

    logger_.info("FIX gateway stopped");
}

void FixGateway::doAccept() {
    if (!running_.load(std::memory_order_acquire))
        return;

    acceptor_.async_accept(
        [this](const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket) {
            onAccept(ec, std::move(socket));
        });
}

void FixGateway::onAccept(const boost::system::error_code& ec,
                          boost::asio::ip::tcp::socket socket) {
    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;  // Gateway is shutting down
        }
        logger_.error("FIX gateway accept error: ", utils::ShortString::shorten(ec.message()));
        // Continue accepting despite errors
        doAccept();
        return;
    }

    logger_.info("FIX gateway: accepted connection from ",
                 utils::ShortString::shorten(socket.remote_endpoint().address().to_string()), ":",
                 socket.remote_endpoint().port());

    // // Create and start a new FixSession
    auto session = sessions_.create(std::move(socket));
    session->start();

    // Continue accepting
    doAccept();
}

void FixGateway::processResponses(const std::atomic<bool>& is_running) noexcept {
    while (is_running.load(std::memory_order_acquire)) {
        const Response* resp = response_queue_.getNextToRead();
        if (resp == nullptr) {
            // Queue empty — yield to avoid burning CPU while idle.
            std::this_thread::yield();
            continue;
        }

        // Find the session that originated this response.
        auto session = sessions_.find(resp->session_id);
        if (!session) {
            logger_.warn("Skipping response because session was not found, session=",
                         type_safe::get(resp->session_id), " user=", type_safe::get(resp->user_id));
            return;
        }
        // TODO: pass it to session
        response_queue_.updateReadIndex();
    }
}

}  // namespace exchange::fix
