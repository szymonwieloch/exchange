# Exchange Project — Agent Guidelines

Ultra low-latency C++23 exchange/trading system. Every microsecond matters.

## Build & Test

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
ctest --test-dir build
```

- Build dir: `build/`
- Tests: GoogleTest, run via CTest
- CI enforces `-Wall -Wextra -Wpedantic -Werror`

## Code Conventions

- **C++23**, no extensions (`CMAKE_CXX_EXTENSIONS OFF`)
- **No heap allocations in the hot path** — pre-allocate at startup
- **No exceptions in hot-path code** — mark hot functions `noexcept`
- Prefer `std::unique_ptr` over `std::shared_ptr`; raw pointers only for non-owning observers
- Prefer `const`, `explicit`, `[[nodiscard]]`, `override`, `final` where applicable
- Thread safety: lock-free > spinlock > mutex; always document synchronization strategy
- Use `std::array` or fixed-size buffers over `std::vector` in latency-sensitive code

## Review Skill

Use `/cpp-latency-review` (or ask to "review") for structured C++ latency/correctness audits. The skill covers thread safety, memory ordering, allocation patterns, cache locality, branch prediction, and best practices.
