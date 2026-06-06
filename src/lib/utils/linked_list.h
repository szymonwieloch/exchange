#pragma once

namespace utils {
/// Common abstraction for non-owning linked lists
template <typename T>
class LinkedList {
public:
    T* prev = nullptr;
    T* next = nullptr;

    LinkedList() = default;
    LinkedList(T* prev, T* next) : prev(prev), next(next) {}

    void disconnect() {
        if (prev != this) {  // loop, the only element
            if (prev) {
                prev->next = next;
            }
            if (next) {
                next->prev = prev;
            }
        }
        next = nullptr;
        prev = nullptr;
    }

    void disconnectFromRing() noexcept {
        assert(prev);
        assert(next);
        if (prev != this) {  // loop, the only element
            prev->next = next;
            next->prev = prev;
        }
        next = nullptr;
        prev = nullptr;
    }
};
}  // namespace utils