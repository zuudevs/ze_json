#include <ze_json/ondemand/json_value.hpp>

namespace zuu::ondemand {

types::StringView
JsonValue::get_string() NOEXCEPT {
    types::uint64_t start_quote_pos = iter_.peek_position();
    iter_.advance();
    types::uint64_t end_quote_pos = iter_.peek_position();
    iter_.advance();

    const char* str_start = iter_.src() + start_quote_pos + 1;
    types::size_t len     = end_quote_pos - start_quote_pos - 1;

    // TODO: Jika string mengandung escape character (\n, \t, dsb),
    // di sinilah kita memanggil Arena Allocator untuk menyimpan teks
    // unescaped!
    return types::StringView(str_start, len);
}

bool
JsonValue::get_bool() NOEXCEPT {
    char c = iter_.peek_char();
    iter_.advance();
    return c == 't';
}

} // namespace zuu::ondemand