#include <ze_json/core/arena.hpp>

namespace zuu::core {

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

} // namespace zuu::core

EXTERN_C_BEGIN
zuu::core::Arena*
create_arena(void* memory_ptr, zuu::types::size_t total_capacity) NOEXCEPT {
    if (!memory_ptr || total_capacity <= sizeof(zuu::core::Arena))
        [[unlikely]] {
        return nullptr;
    }

    void* buffer_start = static_cast<zuu::types::uint8_t*>(memory_ptr) +
                         sizeof(zuu::core::Arena);
    zuu::types::size_t buffer_capacity =
        total_capacity - sizeof(zuu::core::Arena);
    return new (memory_ptr) zuu::core::Arena(buffer_start, buffer_capacity);
}

void
reset_arena(zuu::core::Arena* arena) NOEXCEPT {
    if (arena) [[likely]] {
        arena->reset();
    }
}

void
destroy_arena(zuu::core::Arena* arena) NOEXCEPT {
    if (arena) [[likely]] {
        arena->~Arena();
    }
}

zuu::types::size_t
get_arena_used_space(zuu::core::Arena* arena) NOEXCEPT {
    return arena->get_used_space();
}
EXTERN_C_END