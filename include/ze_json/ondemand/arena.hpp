#pragma once

#include <ze_json/core/integral.hpp>
#include <ze_json/utils/align.hpp>
#include <ze_json/utils/alloc.hpp>
#include <ze_json/utils/forward.hpp>

namespace zuu::memory {

class Arena {
private:
  struct FinalizerNode {
    void (*destroy_fn)(void *);
    void *object_ptr;
    FinalizerNode *next;
  };

  core::uint8_t *buffer_;
  core::size_t capacity_;
  core::size_t offset_;
  FinalizerNode *finalizers_;

public:
  Arena(void *buffer, core::size_t capacity) NOEXCEPT
      : buffer_(static_cast<core::uint8_t *>(buffer)),
        capacity_(capacity),
        offset_(0),
        finalizers_(nullptr) {}
  ~Arena() { reset(); }

  Arena(const Arena &) = delete;
  Arena &operator=(const Arena &) = delete;

  Arena(Arena &&other) NOEXCEPT : buffer_(other.buffer_),
                                  capacity_(other.capacity_),
                                  offset_(other.offset_),
                                  finalizers_(other.finalizers_) {
    other.buffer_ = nullptr;
    other.capacity_ = 0;
    other.offset_ = 0;
    other.finalizers_ = nullptr;
  }
  inline Arena &operator=(Arena &&other) NOEXCEPT {
    if (this != &other) {
      reset();

      buffer_ = other.buffer_;
      capacity_ = other.capacity_;
      offset_ = other.offset_;
      finalizers_ = other.finalizers_;

      other.buffer_ = nullptr;
      other.capacity_ = 0;
      other.offset_ = 0;
      other.finalizers_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] inline void *allocate(core::size_t size,
                                      core::size_t alignment = 8) NOEXCEPT {
    void *current_ptr = buffer_ + offset_;
    core::size_t space_left = capacity_ - offset_;

    if (utils::align(alignment, size, current_ptr, space_left)) {
      offset_ = capacity_ - space_left;
      offset_ += size;
      return current_ptr;
    }

    return nullptr;
  }

  template <typename T, typename... Args>
  [[nodiscard]] T *make(Args &&...args) NOEXCEPT {
    void *ptr = allocate(sizeof(T), alignof(T));
    if (!ptr) {
      return nullptr;
    }

    T *object = new (ptr) T(utils::forward<Args>(args)...);

    if constexpr (!__is_trivially_destructible(T)) {
      void *fin_ptr = allocate(sizeof(FinalizerNode), alignof(FinalizerNode));
      if (fin_ptr) {
        FinalizerNode *node = new (fin_ptr) FinalizerNode;
        node->destroy_fn = [](void *p) { static_cast<T *>(p)->~T(); };
        node->object_ptr = object;
        node->next = finalizers_;
        finalizers_ = node;
      }
    }

    return object;
  }

  void reset() NOEXCEPT {
    FinalizerNode *current = finalizers_;
    while (current != nullptr) {
      current->destroy_fn(current->object_ptr);
      current = current->next;
    }

    finalizers_ = nullptr;
    offset_ = 0;
  }

  [[nodiscard]] inline core::size_t get_used_space() const NOEXCEPT {
    return offset_;
  }
};

} // namespace zuu::memory