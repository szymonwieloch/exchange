#pragma once

#include <cstdint>
#include <string>

namespace exchange::fix {

inline constexpr uint32_t DEFAULT_HEARTBEAT_INTERVAL = 30;  // seconds

/// Configuration for the FIX gateway server.
struct FixGatewayConfig {
    /// Whether the FIX gateway is enabled.
    bool enabled = false;

    /// TCP port to listen on (FIX default is platform-dependent, often 5001+).
    std::uint16_t port = 5001;

    /// Bind address (0.0.0.0 = all interfaces, 127.0.0.1 = localhost only).
    std::string bind_address = "0.0.0.0";

    /// Number of I/O threads for handling FIX sessions.
    /// When 0, defaults to std::thread::hardware_concurrency().
    std::uint32_t num_threads = 0;

    /// FIX SenderCompID expected from connecting clients.
    std::string sender_comp_id = "CLIENT";

    /// FIX TargetCompID (our identifier).
    std::string target_comp_id = "EXCHANGE";

    /// Heartbeat interval in seconds.
    std::uint32_t heartbeat_interval = DEFAULT_HEARTBEAT_INTERVAL;

    /// CPU core to pin the response dispatch thread to.
    /// -1 = no pinning; >= 0 = pin to that specific core.
    std::optional<int> response_thread_core;
};
}  // namespace exchange::fix