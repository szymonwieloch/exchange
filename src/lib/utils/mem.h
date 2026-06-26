#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <new>
#include <vector>

namespace utils {

/// Fixed-size, pre-allocated object pool. No heap allocations in the hot path.
///
/// All backing storage is allocated once in the constructor via std::vector.
/// allocate() and deallocate() are O(1) — a stack of free indices is maintained
/// so both operations are a single push/pop.  The free stack is initialized in
/// reverse order so allocations proceed from low to high addresses for good
/// cache locality.
///
/// T's constructor is called on allocate() and T's destructor is called on
/// deallocate().  The pool itself destroys any still-allocated objects on
/// destruction.
///
/// When the pool is exhausted (all slots in use), allocate() returns nullptr.
/// Size the pool conservatively at startup.
///
/// @tparam T  The type stored in the pool. No default-constructibility
///            requirement — objects are constructed on demand via placement-new.
template <typename T>
class MemPool final {
public:
    /// Pre-allocates raw storage for @p num_elems objects.
    explicit MemPool(std::size_t num_elems) : store(num_elems) {
        free_stack.reserve(num_elems);
        // Push indices in reverse so allocate() returns low-address slots first.
        for (std::size_t i = num_elems; i > 0; --i) {
            free_stack.push_back(i - 1);
        }
    }

    MemPool(const MemPool &) = delete;
    MemPool &operator=(const MemPool &) = delete;
    MemPool(MemPool &&) = default;
    MemPool &operator=(MemPool &&) = default;

    /// Destroys any objects that are still allocated.
    ~MemPool() noexcept {
        // Sort the free stack so we can find allocated indices by exclusion.
        auto sorted_free = free_stack;
        std::sort(sorted_free.begin(), sorted_free.end());

        std::size_t free_idx = 0;
        for (std::size_t i = 0; i < store.size(); ++i) {
            if (free_idx < sorted_free.size() && sorted_free[free_idx] == i) {
                ++free_idx;  // slot i is free — skip
                continue;
            }
#ifndef NDEBUG
            assert(!store[i].is_free);
            store[i].is_free = true;
#endif
            // Slot i is not in the free stack → still allocated.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<T *>(store[i].storage)->~T();
        }
    }

    /// Allocates a slot from the pool and constructs a T in-place.
    ///
    /// @param args  Arguments forwarded to T's constructor via placement-new.
    /// @return  Pointer to the newly constructed object, or nullptr if the
    ///          pool is exhausted (all slots in use).
    template <typename... Args>
    [[nodiscard]] T *allocate(Args &&...args) noexcept {
        if (free_stack.empty()) [[unlikely]] {
            return nullptr;  // pool exhausted
        }
        const auto index = free_stack.back();
        free_stack.pop_back();
        auto &objBlock = store[index];
#ifndef NDEBUG
        assert(objBlock.is_free);
        objBlock.is_free = false;
#endif
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        T *ret = reinterpret_cast<T *>(objBlock.storage);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        ret = new (ret) T(std::forward<Args>(args)...);  // placement new
        ++count;
        return ret;
    }

    /// Returns a previously allocated object to the pool.
    ///
    /// Calls T's destructor before marking the slot as free.
    ///
    /// @param elem  Pointer obtained from a prior allocate() call.
    void deallocate(T *elem) noexcept {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto elemIndex = (reinterpret_cast<ObjectBlock *>(elem) - store.data());
        assert(elemIndex >= 0 && static_cast<size_t>(elemIndex) < store.size());
#ifndef NDEBUG
        assert(!store[elemIndex].is_free);
        store[elemIndex].is_free = true;
#endif
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        elem->~T();
        --count;
        free_stack.push_back(static_cast<std::size_t>(elemIndex));
    }

    /// Returns the number of currently allocated (in-use) objects.
    [[nodiscard]] std::size_t allocated_count() const noexcept { return count; }

    /// Returns the total capacity of the pool.
    [[nodiscard]] std::size_t capacity() const noexcept { return store.size(); }

private:
    /// Raw aligned storage for a T.
    struct ObjectBlock {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        alignas(T) unsigned char storage[sizeof(T)];
#ifndef NDEBUG
        bool is_free = true;
#endif
    };

    std::vector<ObjectBlock> store;
    std::vector<std::size_t> free_stack;
    std::size_t count = 0;
};

}  // namespace utils