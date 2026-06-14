# Exchange

Ultra low-latency limit order book matching engine written in C++23. Designed for Linux, built for microsecond-level determinism.

## Features

| Feature | Description |
|---|---|
| **Price-Time Priority** | Orders at the same price level execute in FIFO order; bids sorted descending, asks ascending |
| **Multi-Ticker** | Up to 8 concurrent order books (compile-time configurable) |
| **Limit Orders** | New, partial fill, full fill, multi-level sweep |
| **Cancellations** | O(1) average lookup and removal via two-level (UserId, OrderId) hash map |
| **Lock-Free Queues** | SPSC for request/response/MD pipelines; MPSC for async logging |
| **Async Logging** | Background-thread file logger with MPSC ring buffer (8 MB), configurable levels |
| **Prometheus Metrics** | Counters, gauges, histograms; HTTP `/metrics` endpoint via Boost.Beast |
| **CPU Affinity** | Thread pinning via `pthread_setaffinity_np` — configurable per thread |
| **TOML Config** | Strict-mode deserialization with reflect-cpp; rejects unknown fields |
| **TSC Timing** | Nanosecond-precision wall-clock conversion via calibrated `rdtsc` |
| **Zero Heap in Hot Path** | All order structures pre-allocated at startup via `MemPool<T>` |
| **Strong Type Safety** | `type_safe::strong_typedef` prevents accidental mixing of domain types |

## Architecture

```
                    ┌──────────────┐
  Network ─────────►│  Gateway     │──► SPSC Request Queue ──┐
  (Aeron/FIX)       └──────────────┘                         │
                                                    ┌────────▼────────┐
                                                    │  MatchingEngine  │
                                                    │  (single-thread) │
                                                    └────────┬────────┘
                              SPSC Response Queue ◄──────────┤
                              SPSC MD Queue ◄────────────────┘
```

- **Single-threaded matching engine** — no locks, no atomics in the hot path; deterministic ordering
- **Two-level order lookup**: hash by `UserId` → direct array by `OrderId` (O(1) amortized)
- **Dual-chain price levels**: hash-collision chain for O(1) lookup + price-sorted chain for O(1) best bid/ask
- **Intrusive doubly-linked lists** — orders form circular rings at each price level; no separate heap allocations

## Performance

| Operation | Complexity | Notes |
|---|---|---|
| Add Order | O(k + 1) | k = price levels crossed during matching |
| Cancel Order | O(1) avg | Hash lookup + ring removal |
| Find Order | O(1) avg | Hash(UserId) + direct array index |
| Get Best Price | O(1) | Head of price-sorted chain |

**Compile-time sizing constants** (`constants.h`):

| Constant | Default | Description |
|---|---|---|
| `MAX_TICKERS` | 8 | Concurrent order books |
| `MAX_ORDER_IDS` | 256K | Unique order IDs per ticker |
| `MAX_PRICE_LEVELS` | 256 | Distinct price levels per side |
| `MAX_ACTIVE_USERS` | 1024 | Concurrent users |

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| type_safe | 0.2.4 | Zero-overhead strong typedefs |
| toml++ | 3.4.0 | TOML configuration parsing |
| reflect-cpp | — | Reflection-based TOML deserialization |
| Boost.Asio + Beast | — | HTTP metrics server |
| Boost.ProgramOptions | — | CLI argument parsing |
| GoogleTest | 1.15.2 | Unit testing framework |
| debug_assert | 1.3.3 | Lightweight assertion macros (type_safe dependency) |

All dependencies are fetched automatically via CMake `FetchContent`.

## Build & Test

```bash
# Configure and build
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# Run tests
ctest --test-dir build

# Enable code coverage (optional)
cmake .. -DCODE_COVERAGE=ON && make -j$(nproc)
```

Compiler flags: `-Wall -Wextra -Wpedantic -Werror`. C++23, no extensions.

## Configuration

```toml
[logging]
level = "info"          # debug | info | warn | error
file = "exchange.log"

[engine]
tickers = ["AAPL", "GOOG", "MSFT", "TSLA", "NVDA", "AMD", "INTC", "IBM"]

[threading]
engine_core = 0         # CPU core for matching engine (-1 = no pinning)
logger_core = 1         # CPU core for background logger

[metrics]
enabled = true
port = 9090
bind_address = "127.0.0.1"
```

## Test Suite

| Test File | Coverage |
|---|---|
| `test_book.cpp` | OrderBook: add, cancel, multi-level match, FIFO priority, side isolation |
| `test_orders_at_price.cpp` | Price-level aggregation, ring operations, priority calculation |
| `test_user_orders.cpp` | User order map, (UserId, OrderId) lookups, pool exhaustion |
| `test_queue.cpp` | SPSC/MPSC lock-free queues, FIFO ordering, concurrent producer/consumer |
| `test_mempool.cpp` | Pool allocation/deallocation, exhaustion, constructor/destructor invocation |
| `test_log.cpp` | Async logging, ring buffer, multi-threaded logging, flush to disk |
| `test_metrics.cpp` | Counter increment, Gauge set/inc/dec, Histogram with bucket tracking |
| `test_config.cpp` | TOML parsing, defaults, strict validation, log-level validation |
| `test_thread.cpp` | CPU affinity pinning |
| `test_time.cpp` | TSC calibration, TSC → wall-clock conversion, timestamp formatting |

## Roadmap

- [ ] Network gateway / API layer (Aeron/SBE, FIX protocol)
- [ ] Market data broadcast
- [ ] Load generator for benchmarking
- [ ] Docker Compose setup (exchange + load generator + Aeron)
- [ ] CI pipeline cleanup and hardening
- [ ] Additional metrics and observability endpoints

## License

See [LICENSE](./LICENSE).
