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
};
}  // namespace utils