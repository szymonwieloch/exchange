#pragma once
#include <memory>
#include <unordered_map>

#include "fix_session.hpp"
#include "lib/exchange/definitions.h"

namespace exchange::fix {
/// Thread-safe collection of FIX sessions, indexable by SessionId (unique)
/// and by UserId (non-unique — a user may have multiple sessions).
///
/// Sessions are registered by SessionId at creation time and by UserId
/// after a successful Logon when the user identity is known.
class FixSessions {
public:
    FixSessions(utils::Logger& logger, const AssetTranslator& translator,
                RequestLFQueue& request_queue, UserManager& user_mgr, FixSessionConfig ses_cfg)
        : logger_(logger),
          translator_(translator),
          request_queue_(request_queue),
          user_mgr_(user_mgr),
          ses_cfg_(ses_cfg) {}

    /// Looks up a session by its unique SessionId.
    std::shared_ptr<FixSession> find(SessionId sessionId) const noexcept {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            return {};
        }
        return it->second.lock();
    }

    /// Looks up a session by UserId (non-unique — returns the first live session).
    std::shared_ptr<FixSession> findByUser(UserId userId) const noexcept {
        std::lock_guard lock(mutex_);
        auto it = user_sessions_.find(userId);
        if (it == user_sessions_.end()) {
            return {};
        }
        return it->second.lock();
    }

    /// Creates a new session and registers it by SessionId.
    std::shared_ptr<FixSession> create(boost::asio::ip::tcp::socket socket) {
        auto id = SessionId(next_id_++);
        auto session = std::make_shared<FixSession>(id, std::move(socket), translator_, ses_cfg_,
                                                    request_queue_, logger_, user_mgr_, *this);
        std::lock_guard lock(mutex_);
        sessions_[id] = session;
        return session;
    }

    /// Registers a session's UserId (called after successful Logon).
    void registerUser(SessionId id, UserId userId) noexcept {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return;
        }
        user_sessions_[userId] = it->second;
    }

    /// Removes a session from both indices.
    void remove(SessionId id) noexcept {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(id);
        if (it != sessions_.end()) {
            // If the session has a known user, clean up the user index.
            if (auto session = it->second.lock()) {
                auto uit = user_sessions_.find(session->getUserId());
                if (uit != user_sessions_.end() && uit->second.lock() == session) {
                    user_sessions_.erase(uit);
                }
            }
            sessions_.erase(it);
        }
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<SessionId, std::weak_ptr<FixSession>> sessions_;
    std::unordered_map<UserId, std::weak_ptr<FixSession>> user_sessions_;
    std::atomic<std::uint32_t> next_id_{0};
    utils::Logger& logger_;
    const AssetTranslator& translator_;
    RequestLFQueue& request_queue_;
    UserManager& user_mgr_;
    FixSessionConfig ses_cfg_;
};
}  // namespace exchange::fix