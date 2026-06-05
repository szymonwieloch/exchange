#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <vector>

namespace utils {

/// Fixed-size, pre-allocated object pool. No heap allocations in the hot path.
///
/// All backing storage is allocated once in the constructor via std::vector.
/// allocate() and deallocate() are O(1) amortized — they scan forward from
/// the last-known free slot until a free slot is found, wrapping around at
/// the end.  This gives good cache locality under sequential allocation
/// patterns.
///
/// T's constructor is called on allocate() and T's destructor is called on
/// deallocate().  The pool itself destroys any still-allocated objects on
/// destruction.
///
/// @warning  The pool asserts (terminates) when all slots are exhausted.
///           Size the pool conservatively at startup.
///
/// @tparam T  The type stored in the pool. No default-constructibility
///            requirement — objects are constructed on demand via placement-new.
template <typename T>
class MemPool final {
public:
    /// Pre-allocates raw storage for @p num_elems objects.
    explicit MemPool(std::size_t num_elems) : store(num_elems) {}

    /// Destroys any objects that are still allocated.
    ~MemPool() noexcept {
        for (auto &block : store) {
            if (!block.is_free) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<T *>(block.storage)->~T();
            }
        }
    }

    /// Allocates a slot from the pool and constructs a T in-place.
    ///
    /// @param args  Arguments forwarded to T's constructor via placement-new.
    /// @return  Pointer to the newly constructed object.
    /// @pre  At least one free slot must exist (enforced by assert).
    template <typename... Args>
    T *allocate(Args &&...args) noexcept {
        auto &objBlock = store[next_free_index];
        assert(objBlock.is_free);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        T *ret = reinterpret_cast<T *>(objBlock.storage);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        ret = new (ret) T(std::forward<Args>(args)...);  // placement new
        objBlock.is_free = false;
        updateNextFreeIndex();
        return ret;
    }

    /// Returns a previously allocated object to the pool.
    ///
    /// Calls T's destructor before marking the slot as free.
    ///
    /// @param elem  Pointer obtained from a prior allocate() call.
    auto deallocate(T *elem) noexcept {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto elemIndex = (reinterpret_cast<ObjectBlock *>(elem) - store.data());
        assert(elemIndex >= 0 && static_cast<size_t>(elemIndex) < store.size());
        assert(!store[elemIndex].is_free);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        elem->~T();
        store[elemIndex].is_free = true;
    }

private:
    /// Raw aligned storage for a T, paired with a free/in-use flag.
    struct ObjectBlock {
        alignas(T) unsigned char storage[sizeof(T)];
        bool is_free = true;
    };

    /// Advances next_free_index to the next free slot, wrapping if necessary.
    ///
    /// Asserts when no free slot is found (pool exhausted).
    auto updateNextFreeIndex() noexcept {
        [[maybe_unused]] const auto initialFreeIndex = next_free_index;
        while (!store[next_free_index].is_free) {
            ++next_free_index;
            if (next_free_index == store.size()) [[unlikely]] {
                next_free_index = 0;
            }
        }
        assert(initialFreeIndex != next_free_index);  // out of space in the pool
    }

    std::vector<ObjectBlock> store;
    size_t next_free_index = 0;
};

}  // namespace utils