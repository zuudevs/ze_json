#pragma once

#include <ze_json/ondemand/json_iterator.hpp>
#include <ze_json/ondemand/json_type.hpp>
#include <ze_json/types/string_view.hpp>
#include <ze_json/utils/number_parser.hpp>

namespace zuu::ondemand {

class JsonValue {
  private:
    // Pointer (bukan referensi) agar JsonValue bisa di-copy-assign — kursor
    // yang di-copy seluruhnya menunjuk ke iterator yang sama, sehingga state
    // kursor terbagi (onedemand forward-only per satu Document).
    JsonIterator* iter_;

  public:
    constexpr explicit JsonValue(JsonIterator& iter) NOEXCEPT : iter_(&iter) {}

    // Akses karakter struktural saat ini. Dipakai lapisan API publik untuk
    // membedakan nilai angka (tidak direkam tape; posisinya berakhir di
    // karakter struktural penerus) dari bracket penutup objek/array.
    [[nodiscard]] constexpr char
    peek_char() const NOEXCEPT {
        return iter_->peek_char();
    }

    [[nodiscard]] constexpr JsonType
    type() const NOEXCEPT {
        auto type = lookups::kStructural[static_cast<types::uint8_t>(
                        iter_->peek_char())] &
                    lookups::TYPE_MASK;
        return type == 0 ? JsonType::Number : static_cast<JsonType>(type);
    }

    // Angka tidak direkam oleh scanner; teksnya berada di antara entry
    // struktural sebelumnya (':' / ',' / '[') dan entry struktural saat ini
    // (',' / '}' / ']') yang menjadi terminator angka.
    // Habiskan nilai yang sedang dipegang kursor TANPA membacanya:    // string = 2 entry, literal = 1 entry, kontainer = bracket-depth walk,
    // angka = 0 entry (kursor sudah berada di karakter / terminator
    // penerusnya). Dipakai traversal array untuk elemen yang sengaja tidak
    // dibaca, dan sebagai langkah lintas-separator angka.
    constexpr void
    skip() NOEXCEPT {
        iter_->skip_child();
    }

    // True bila kursor sedang menunjuk elemen angka (0 entry tape) — gap teks
    // antar entry berawalan digit/tanda. Juga dipakai array_next untuk
    // membedakan entry separator ',' dari terminator elemen angka (gap
    // setelah literal 'true' adalah "rue", jadi has_inline_text saja tidak
    // cukup).
    [[nodiscard]] constexpr bool
    has_inline_number() const NOEXCEPT {
        return iter_->has_inline_number();
    }

    [[nodiscard]] constexpr double
    get_double() NOEXCEPT {
        const char* p = iter_->src() + iter_->peek_prev_position() + 1;
        iter_->advance();
        return utils::parse_double(p);
    }

    [[nodiscard]] constexpr types::int64_t
    get_int() NOEXCEPT {
        const char* p = iter_->src() + iter_->peek_prev_position() + 1;
        iter_->advance();
        return utils::parse_int(p);
    }

    [[nodiscard]] types::StringView
    get_string() NOEXCEPT;

    [[nodiscard]] bool
    get_bool() NOEXCEPT;

    [[nodiscard]] JsonObject
    get_object() NOEXCEPT;

    [[nodiscard]] JsonArray
    get_array() NOEXCEPT;
};

} // namespace zuu::ondemand