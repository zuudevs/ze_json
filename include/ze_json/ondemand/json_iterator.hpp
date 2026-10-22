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
    void* arena_;

  public:
    constexpr JsonIterator(
        const char* src, const types::uint64_t* tape, types::size_t tape_len,
        void* arena = nullptr) NOEXCEPT : src_(src),
                                          tape_(tape),
                                          tape_len_(tape_len),
                                          current_idx_(0),
                                          arena_(arena) {}

    constexpr void*
    arena() const noexcept {
        return arena_;
    }
    constexpr void
    set_arena(void* arena) noexcept {
        arena_ = arena;
    }

    constexpr char
    peek_char() const NOEXCEPT {
        return current_idx_ >= tape_len_ ? '\0' : src_[tape_[current_idx_]];
    }

    constexpr types::uint64_t
    peek_position() const NOEXCEPT {
        return tape_[current_idx_];
    }

    constexpr void
    advance() NOEXCEPT {
        if (current_idx_ < tape_len_) {
            current_idx_++;
        }
    }

    // Dapatkan posisi tape entry sebelum entry saat ini. Dipakai nilai (angka)
    // yang tidak direkam scanner, karena teks angka berada di antara entry
    // struktural sebelumnya dan entry saat ini.
    constexpr types::uint64_t
    peek_prev_position() const NOEXCEPT {
        return current_idx_ == 0 ? 0 : tape_[current_idx_ - 1];
    }

    // True bila teks payload di antara entry tape SEBELUMNYA dan entry saat
    // ini memuat minimal satu karakter non-whitespace. Dipakai untuk
    // membedakan elemen angka (0 entry tape; kursor berada di terminatornya)
    // dari sekadar entry separator ','.
    constexpr bool
    has_inline_text() const NOEXCEPT {
        if (current_idx_ == 0) {
            return false;
        }

        const types::uint64_t prev_pos = tape_[current_idx_ - 1];
        const types::uint64_t cur_pos  = tape_[current_idx_];
        for (const char* p = src_ + prev_pos + 1; p < src_ + cur_pos; ++p) {
            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                return true;
            }
        }
        return false;
    }

    // Varian yang HANYA true bila teks di antara dua entry berawalan
    // karakter angka / tanda ('0'..'9', '-', '+', '.'). Dipakai traversal
    // array: separator ',' setelah elemen literal ('true' menyisakan gap
    // "rue") TIDAK boleh dianggap terminator angka.
    constexpr bool
    has_inline_number() const NOEXCEPT {
        if (current_idx_ == 0) {
            return false;
        }

        const types::uint64_t prev_pos = tape_[current_idx_ - 1];
        const types::uint64_t cur_pos  = tape_[current_idx_];
        for (const char* p = src_ + prev_pos + 1; p < src_ + cur_pos; ++p) {
            if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                continue;
            }
            const char c = *p;
            return c == '-' || c == '+' || c == '.' ||
                   (c >= '0' && c <= '9');
        }
        return false;
    }

    constexpr void
    skip_child() NOEXCEPT {
        char c     = peek_char();
        auto flags = lookups::kStructural[static_cast<types::uint8_t>(c)];

        if (flags & lookups::SCAN_QUOTE) {
            // String = tepat 2 tape entry (quote pembuka & penutup)
            advance();
            advance();
            return;
        }

        if ((flags & lookups::PAIR_OPEN) == 0) {
            // Scalar: literal 't'/'f'/'n' menempati 1 entry, sedangkan angka
            // menempati 0 entry (iterasi sudah berada pada karakter struktural
            // setelah angka). Satu langkah cukup untuk keduanya.
            advance();
            return;
        }

        // Kontainer: lakukan bracket depth scanning. flags WAJIB dihitung
        // ulang pada setiap iterasi karena karakter berubah.
        int depth = 0;
        do {
            c     = peek_char();
            flags = lookups::kStructural[static_cast<types::uint8_t>(c)];
            if ((flags & lookups::PAIR_OPEN) != 0) {
                depth++;
            } else if ((flags & lookups::PAIR_CLOSE) != 0) {
                depth--;
            }
            advance();
        } while (depth > 0 && current_idx_ < tape_len_);
    }

    constexpr const char*
    src() const NOEXCEPT {
        return src_;
    }

    // Kembalikan kursor ke awal dokumen. Dipakai pencarian path: tiap query
    // path melintasi ulang dari root (kursor forward-only, jadi satu-satunya
    // cara kueri independen — biayanya O(panjang path), tetap skala nanosekon).
    constexpr void
    reset() NOEXCEPT {
        current_idx_ = 0;
    }
    constexpr const types::uint64_t*
    tape() const NOEXCEPT {
        return tape_;
    }
    constexpr types::size_t
    index() const NOEXCEPT {
        return current_idx_;
    }

    friend class JsonValue;
    friend class JsonObject;
    friend class JsonArray;
};

} // namespace zuu::ondemand