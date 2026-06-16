#pragma once
#include <memory>
#include <unordered_map>

#include "fix_session.hpp"
#include "lib/exchange/definitions.h"

namespace exchange::fix {
class FixSessions {
public:
    FixSessions(utils::Logger& logger, const AssetTranslator& translator,
                RequestLFQueue& request_queue, UserManager& user_mgr, FixSessionConfig ses_cfg)
        : logger_(logger),
          translator_(translator),
          request_queue_(request_queue),
          user_mgr_(user_mgr),
          ses_cfg_(ses_cfg) {}
    std::shared_ptr<FixSession> find(SessionId sessionId, UserId /*user_id*/) const noexcept {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            return {};
        } else {
            return it->second.lock();
        }
    }
    std::shared_ptr<FixSession> create(boost::asio::ip::tcp::socket socket) {
        auto id = SessionId(next_id_++);
        auto session = std::make_shared<FixSession>(id, std::move(socket), translator_, ses_cfg_,
                                                    request_queue_, logger_, user_mgr_, *this);
        std::lock_guard lock(mutex_);
        sessions_[id] = session;
        return session;
    }

    void remove(SessionId id) noexcept {
        std::lock_guard lock(mutex_);
        sessions_.erase(id);
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<SessionId, std::weak_ptr<FixSession>> sessions_;
    std::atomic<std::uint32_t> next_id_{0};
    utils::Logger& logger_;
    const AssetTranslator& translator_;
    RequestLFQueue& request_queue_;
    UserManager& user_mgr_;
    FixSessionConfig ses_cfg_;  // TODO
};
}  // namespace exchange::fix