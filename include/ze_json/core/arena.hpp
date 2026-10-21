#pragma once

#include <ze_json/types/integral.hpp>
#include <ze_json/utils/align.hpp>
#include <ze_json/utils/alloc.hpp>
#include <ze_json/utils/forward.hpp>

namespace zuu::core {

class Arena {
  private:
    struct FinalizerNode {
        void (*destroy_fn)(void*);
        void* object_ptr;
        FinalizerNode* next;
    };

    types::uint8_t* buffer_;
    types::size_t capacity_;
    types::size_t offset_;
    FinalizerNode* finalizers_;

  public:
    Arena(void* buffer, types::size_t capacity) NOEXCEPT;
    ~Arena();

    Arena(const Arena&) = delete;
    Arena&
    operator=(const Arena&) = delete;

    Arena(Arena&& other) NOEXCEPT;
    Arena&
    operator=(Arena&& other) NOEXCEPT;

    [[nodiscard]] void*
    allocate(types::size_t size, types::size_t alignment = 8) NOEXCEPT;

    template <typename T, typename... Args>
    [[nodiscard]] T*
    make(Args&&... args) NOEXCEPT {
        void* ptr = allocate(sizeof(T), alignof(T));
        if (!ptr) {
            return nullptr;
        }

        T* object = new (ptr) T(utils::forward<Args>(args)...);

        if constexpr (!__is_trivially_destructible(T)) {
            void* fin_ptr =
                allocate(sizeof(FinalizerNode), alignof(FinalizerNode));
            if (fin_ptr) {
                FinalizerNode* node = new (fin_ptr) FinalizerNode;
                node->destroy_fn    = [](void* p) { static_cast<T*>(p)->~T(); };
                node->object_ptr    = object;
                node->next          = finalizers_;
                finalizers_         = node;
            }
        }

        return object;
    }

    void
    reset() NOEXCEPT;
    [[nodiscard]] types::size_t
    get_used_space() const NOEXCEPT;
};

EXTERN_C_BEGIN

[[nodiscard]] zuu::core::Arena*
create_arena(void* memory_ptr, types::size_t total_capacity) NOEXCEPT;
void
reset_arena(zuu::core::Arena* arena) NOEXCEPT;
void
destroy_arena(zuu::core::Arena* arena) NOEXCEPT;

EXTERN_C_END

} // namespace zuu::core