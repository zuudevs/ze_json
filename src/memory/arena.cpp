#include <ze_json/memory/arena.hpp>

namespace zuu::memory {

Arena::Arena(void* buffer, types::size_t capacity) NOEXCEPT
    : buffer_(static_cast<types::uint8_t*>(buffer)),
      capacity_(capacity),
      offset_(0),
      finalizers_(nullptr) {}

Arena::~Arena() { reset(); }

Arena::Arena(Arena&& other) NOEXCEPT : buffer_(other.buffer_),
                                       capacity_(other.capacity_),
                                       offset_(other.offset_),
                                       finalizers_(other.finalizers_) {
    other.buffer_     = nullptr;
    other.capacity_   = 0;
    other.offset_     = 0;
    other.finalizers_ = nullptr;
}

Arena&
Arena::operator=(Arena&& other) NOEXCEPT {
    if (this != &other) {
        reset();

        buffer_     = other.buffer_;
        capacity_   = other.capacity_;
        offset_     = other.offset_;
        finalizers_ = other.finalizers_;

        other.buffer_     = nullptr;
        other.capacity_   = 0;
        other.offset_     = 0;
        other.finalizers_ = nullptr;
    }
    return *this;
}

void*
Arena::allocate(types::size_t size, types::size_t alignment) NOEXCEPT {
    void* current_ptr        = buffer_ + offset_;
    types::size_t space_left = capacity_ - offset_;

    if (utils::align(alignment, size, current_ptr, space_left)) {
        offset_ = capacity_ - space_left;
        offset_ += size;
        return current_ptr;
    }

    return nullptr;
}

void
Arena::reset() NOEXCEPT {
    FinalizerNode* current = finalizers_;
    while (current != nullptr) {
        current->destroy_fn(current->object_ptr);
        current = current->next;
    }

    finalizers_ = nullptr;
    offset_     = 0;
}

types::size_t
Arena::get_used_space() const NOEXCEPT {
    return offset_;
}

} // namespace zuu::memory