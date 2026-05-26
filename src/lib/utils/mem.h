#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <vector>

namespace utils {

template <typename T>
class MemPool final {
public:
    explicit MemPool(std::size_t num_elems)
        : store(num_elems, {T(), true}) {}

    template <typename... Args>
    T *allocate(Args... args) noexcept {
        auto objBlock = &(store[next_free_index]);
        assert(objBlock->is_free);
        T *ret = &(objBlock->object_);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        ret = new (ret) T(args...);  // placement new
        objBlock->is_free = false;
        updateNextFreeIndex();
        return ret;
    }

private:
    struct ObjectBlock {
        T object_;
        bool is_free = true;
    };

    auto updateNextFreeIndex() noexcept {
        const auto initialFreeIndex = next_free_index;
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