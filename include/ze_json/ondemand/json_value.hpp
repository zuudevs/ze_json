#pragma once

#include <ze_json/types/string_view.hpp>
#include <ze_json/ondemand/json_type.hpp>
#include <ze_json/ondemand/json_iterator.hpp>

namespace zuu::ondemand {

class JsonValue {
private:
    JsonIterator& iter_;

public:
    constexpr explicit JsonValue(JsonIterator& iter) noexcept : iter_(iter) {}

    constexpr JsonType type() const noexcept {
        auto type = lookups::kStructural[static_cast<types::uint8_t>(iter_.peek_char())] & lookups::TYPE_MASK;
		return type == 0 ? JsonType::Number : static_cast<JsonType>(type);
    }

    types::StringView get_string() noexcept {
        types::uint64_t start_quote_pos = iter_.peek_position();
        iter_.advance();
        types::uint64_t end_quote_pos = iter_.peek_position();
        iter_.advance();

        const char* str_start = iter_.src() + start_quote_pos + 1;
        types::size_t len = end_quote_pos - start_quote_pos - 1;
        
        // TODO: Jika string mengandung escape character (\n, \t, dsb),
        // di sinilah kita memanggil Arena Allocator untuk menyimpan teks unescaped!
        return types::StringView(str_start, len);
    }

    bool get_bool() noexcept {
        char c = iter_.peek_char();
        iter_.advance();
        return c == 't';
    }

    JsonObject get_object() noexcept;
    JsonArray get_array() noexcept;
};

} // namespace zuu::ondemand