#pragma once

#include <ze_json/ondemand/json_value.hpp>
#include <ze_json/types/string_view.hpp>

namespace zuu::ondemand {

class JsonObject {
  private:
    JsonIterator& iter_;

  public:
    constexpr explicit JsonObject(JsonIterator& iter) NOEXCEPT : iter_(iter) {
        iter_.advance();
    }

    JsonValue
    find_field(const types::StringView& target_key) NOEXCEPT;

    // Varian yang melaporkan status pencarian. Dibutuhkan API publik
    // (export ke JS) karena nilai angka tidak direkam di tape, sehingga
    // hasil find_field tidak bisa dideteksi 'tidak ditemukan' dari
    // tipe nilainya saja.
    bool
    find_field_checked(
        const types::StringView& target_key, JsonValue* out) NOEXCEPT;
};

} // namespace zuu::ondemand