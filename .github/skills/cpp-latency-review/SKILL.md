---
name: cpp-latency-review
description: 'Review C++ code for ultra low-latency correctness, performance pitfalls, and best practices. Use when: reviewing PRs, auditing C++ changes, checking for latency regressions, validating thread safety, examining hot-path code, verifying lock-free patterns, or any code review task in this project.'
argument-hint: '[file or PR description]'
---

# C++ Ultra Low-Latency Code Review

## Overview

This skill provides a structured, rigorous review checklist for C++ code in an ultra low-latency exchange/trading context. Every microsecond matters — the review focuses on correctness, determinism, and the elimination of latency jitter.

## When to Use

- Reviewing any pull request or diff in this project
- Auditing hot-path code for latency regressions
- Validating lock-free or wait-free data structures
- Checking thread safety and memory ordering
- Assessing new algorithms or data structure choices
- Any time the user asks to "review", "audit", or "check" C++ code

## Review Procedure

For every code change, go through the categories below in order. Flag issues with severity: **🔴 Critical** (correctness/data-race/UB), **🟠 High** (latency regression), **🟡 Medium** (code quality/maintainability), **⚪ Info** (style/nit).

---

### 1. Correctness & Undefined Behavior (🔴 Critical)

- **Thread safety**: Is shared state protected? Are mutexes held for the minimum necessary duration? Could this deadlock?
- **Memory ordering**: Are `std::atomic` operations using the correct memory orders? Is `memory_order_seq_cst` used unnecessarily (it inserts expensive fence instructions)? Prefer `acquire/release` semantics; use `relaxed` only when provably correct.
- **Data races**: Does any non-atomic variable get written from one thread and read from another without synchronization? Check lambdas captured by reference in `std::thread`/`std::async`.
- **Lifetime issues**: Are objects guaranteed to outlive their references/pointers? Watch for capturing `this` in callbacks.
- **Signed integer overflow**: Is undefined. For performance counters use unsigned types, or check bounds explicitly.
- **Null pointer dereference**: Check all pointer dereferences. Prefer references over pointers when null is not a valid state.
- **Double delete / use-after-free**: Verify ownership semantics. Prefer `std::unique_ptr` for clear ownership.
- **Exception safety**: In the hot path, exceptions are unacceptable. Ensure `noexcept` on all hot-path functions. In non-hot-path code, ensure strong/weak exception guarantees where appropriate.

### 2. Latency & Performance (🟠 High)

#### Memory Allocation
- **No heap allocations in the hot path**: Zero `new`, `malloc`, `std::vector::push_back` (which may reallocate), `std::make_shared` in latency-sensitive code. Pre-allocate at startup.
- **Stack allocation**: Prefer stack-allocated `std::array` or fixed-size buffers over `std::vector`.
- **Custom allocators**: If dynamic allocation is unavoidable, verify pool/arena allocators with O(1) allocation are used.

#### Cache Locality
- **False sharing**: Are hot-path atomic or frequently-written variables in the same cache line as other hot data? Use `alignas(64)` (or `std::hardware_destructive_interference_size` in C++23) to separate.
- **Data layout**: Are structs organized for cache efficiency? Group frequently-accessed fields together. Consider AoS vs SoA for hot loops.
- **Prefetching**: For linked structures, is `__builtin_prefetch` used where appropriate?

#### Branch Prediction
- **Predictable branches**: Are hot-path branches predictable? Consider using `__builtin_expect` (`likely`/`unlikely` macros) for error/uncommon paths.
- **Branchless alternatives**: Can `if/else` in the hot path be replaced with branchless arithmetic (e.g., `(a < b) * x + (a >= b) * y`)?
- **Virtual function calls**: Avoid `virtual` dispatch in the hot path. Use CRTP (`static` polymorphism) or `std::variant` + `std::visit` if dispatch is unavoidable.

#### System Calls & Blocking
- **No blocking calls in the hot path**: No mutex contention (use lock-free or try-lock), no I/O, no `sleep`, no `malloc`/`free`.
- **System call avoidance**: `gettimeofday`, `clock_gettime` — are these called in hot path? If timestamps are needed, consider `rdtsc` or pre-sampled clock values.
- **Page faults**: Is memory touched/pre-faulted at startup? Use `mlockall` or touch all allocated memory to prevent minor page faults.

#### Data Structure Choices
- **Flat structures over node-based**: `std::array`/`std::vector` over `std::list`/`std::map`. Node-based containers cause cache misses.
- **Hash table quality**: Are hash tables (`std::unordered_map`) using a good hash function? Consider open-addressing alternatives (like `absl::flat_hash_map` or `robin_hood`).
- **Sorting**: For small N, insertion sort or sorting networks beat `std::sort`. Consider radix sort for integer keys.

### 3. General C++ Best Practices (🟡 Medium)

#### Modern C++ Usage
- **RAII**: Are resources (memory, file handles, locks) managed by RAII types? No manual `new`/`delete`; use `std::unique_ptr`/`std::make_unique`.
- **`const` correctness**: Are member functions marked `const` when they don't mutate state? Are parameters `const` where appropriate?
- **`noexcept`**: Are move constructors, swap functions, and hot-path functions marked `noexcept`?
- **`explicit`**: Single-argument constructors and conversion operators marked `explicit`.
- **`[[nodiscard]]`**: Used on functions where ignoring the return value is a bug?
- **`override`**: Used on all overriding virtual functions?
- **`final`**: Used on classes not designed for inheritance, or on virtual functions that shouldn't be overridden further?

#### Code Quality
- **Magic numbers**: Are unexplained literals replaced with named constants or `constexpr`?
- **Error handling**: Is error handling consistent? Does it use `std::expected` (C++23), `tl::expected`, or error codes rather than exceptions in hot paths?
- **Logging**: Is logging in the hot path? Logging involves I/O and allocations — it must be deferred or async.
- **Headers**: Are includes minimal and necessary? Use forward declarations in headers to reduce compile times.
- **`assert` usage**: Are preconditions checked with `assert` (debug-only) or runtime checks? In production, assert violations in hot path can be catastrophic.

### 4. Testing & Validation (🟡 Medium)

- **Unit tests for edge cases**: Zero, negative, max/min values, empty containers, boundary conditions.
- **Concurrency tests**: Is there a test that exercises multi-threaded scenarios? ThreadSanitizer (`-fsanitize=thread`) should pass cleanly.
- **Benchmarks**: Is the latency impact of this change measured? Use `std::chrono::steady_clock` or hardware counters.
- **Determinism**: For exchange code, is behavior deterministic given identical inputs?

---

## Common Anti-Patterns in Low-Latency C++

| Anti-Pattern | Why It's Bad | Fix |
|---|---|---|
| `std::shared_ptr` everywhere | Atomic ref-counting causes cache-line bouncing | `std::unique_ptr` + raw reference for observers |
| `std::vector::push_back` in loop | Repeated allocations and copies | `reserve()` or pre-size the vector |
| `std::map`/`std::set` | Node-based, cache-unfriendly | `std::vector` + sort + `std::binary_search`, or flat sets |
| `std::function` | Type erasure + potential allocation | Template the callable, or use `function_ref` |
| `dynamic_cast` | RTTI lookup, unpredictable | `static_cast` with known types, or `std::variant` + `visit` |
| `std::endl` | Flushes the stream (I/O) | Use `'\n'` |
| Copying large objects | Cache pollution | Pass by const reference, or move |
| `std::mutex` in hot path | Kernel transition on contention | Lock-free, or `std::atomic_flag` spinlock |

---

## Output Format

When reviewing, produce a structured report:

```
## Review: [file(s) or PR description]

### Critical Issues 🔴
- [file:line] Issue — fix suggestion

### Latency/Performance 🟠
- [file:line] Issue — fix suggestion

### Best Practices 🟡
- [file:line] Issue — fix suggestion

### Summary
X critical, Y performance, Z best-practice issues found.
```
