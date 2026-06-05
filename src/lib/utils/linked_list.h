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
        if (prev) {
            prev->next = next;
        }
        if (next) {
            next->prev = prev;
        }
        next = nullptr;
        prev = nullptr;
    }
};
}  // namespace utils