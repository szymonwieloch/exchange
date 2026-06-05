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
/// When the pool is exhausted (all slots in use), allocate() returns nullptr.
/// Size the pool conservatively at startup.
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
    /// @return  Pointer to the newly constructed object, or nullptr if the
    ///          pool is exhausted (all slots in use).
    template <typename... Args>
    T *allocate(Args &&...args) noexcept {
        if (count == store.size()) [[unlikely]] {
            return nullptr;  // pool exhausted
        }
        auto &objBlock = store[next_free_index];
        assert(objBlock.is_free);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        T *ret = reinterpret_cast<T *>(objBlock.storage);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        ret = new (ret) T(std::forward<Args>(args)...);  // placement new
        objBlock.is_free = false;
        ++count;
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
        --count;
    }

    /// Returns the number of currently allocated (in-use) objects.
    [[nodiscard]] std::size_t allocated_count() const noexcept { return count; }

    /// Returns the total capacity of the pool.
    [[nodiscard]] std::size_t capacity() const noexcept { return store.size(); }

private:
    /// Raw aligned storage for a T, paired with a free/in-use flag.
    struct ObjectBlock {
        alignas(T) unsigned char storage[sizeof(T)];
        bool is_free = true;
    };

    /// Advances next_free_index to the next free slot, wrapping if necessary.
    ///
    /// If no free slot exists (pool is full), next_free_index is unchanged.
    auto updateNextFreeIndex() noexcept {
        const auto start = next_free_index;
        do {
            ++next_free_index;
            if (next_free_index == store.size()) [[unlikely]] {
                next_free_index = 0;
            }
        } while (!store[next_free_index].is_free && next_free_index != start);
    }

    std::vector<ObjectBlock> store;
    std::size_t next_free_index = 0;
    std::size_t count = 0;
};

}  // namespace utils