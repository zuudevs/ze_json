#pragma once

#include <ze_json/ondemand/json_array.hpp>

namespace zuu::ondemand {

class Document {
  private:
    JsonIterator iter_;

  public:
    constexpr Document(
        const char* RESTRICT src, const types::uint64_t* RESTRICT tape,
        types::size_t tape_len, void* RESTRICT arena) NOEXCEPT
        : iter_(src, tape, tape_len, arena) {}

    [[nodiscard]] JsonValue
    get_root() NOEXCEPT;

    // Kembalikan kursor ke posisi root (idx 0) tanpa membatalkan arena.
    void
    rewind() NOEXCEPT {
        iter_.reset();
    }
};

} // namespace zuu::ondemand