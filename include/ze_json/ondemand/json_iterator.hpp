#pragma once

#include <ze_json/lookups/structural.hpp>

namespace zuu::ondemand {

class JsonValue;
class JsonObject;
class JsonArray;
class JsonField;

class JsonIterator {
  private:
    const char* src_;
    const types::uint64_t* tape_;
    types::size_t tape_len_;
    types::size_t current_idx_;

  public:
    constexpr JsonIterator(
        const char* src, const types::uint64_t* tape,
        types::size_t tape_len) noexcept
        : src_(src), tape_(tape), tape_len_(tape_len), current_idx_(0) {}

    constexpr char
    peek_char() const noexcept {
        return current_idx_ >= tape_len_ ? '\0' : src_[tape_[current_idx_]];
    }

    constexpr types::uint64_t
    peek_position() const noexcept {
        return tape_[current_idx_];
    }

    constexpr void
    advance() noexcept {
        if (current_idx_ < tape_len_) {
            current_idx_++;
        }
    }

    constexpr void
    skip_child() noexcept {
        char c     = peek_char();
        auto flags = lookups::kStructural[static_cast<types::uint8_t>(c)];
        if ((flags & lookups::PAIR_OPEN) != 0) {
            advance();
            if (c == '"') {
                advance();
            }
            return;
        }

        int depth = 0;
        do {
            c = peek_char();
            if (flags & lookups::PAIR_OPEN) {
                depth++;
            } else if (flags & lookups::PAIR_CLOSE) {
                depth--;
            }
            advance();
        } while (depth > 0 && current_idx_ < tape_len_);
    }

    constexpr const char*
    src() const noexcept {
        return src_;
    }
    constexpr const types::uint64_t*
    tape() const noexcept {
        return tape_;
    }
    constexpr types::size_t
    index() const noexcept {
        return current_idx_;
    }

    friend class JsonValue;
    friend class JsonObject;
    friend class JsonArray;
};

} // namespace zuu::ondemand