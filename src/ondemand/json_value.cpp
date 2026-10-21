#include <ze_json/ondemand/json_value.hpp>
#include <ze_json/utils/utf8_encoder.hpp>

namespace zuu::ondemand {

types::StringView
JsonValue::get_string() NOEXCEPT {
    types::uint64_t start_quote_pos = iter_.peek_position();
    iter_.advance();
    types::uint64_t end_quote_pos = iter_.peek_position();
    iter_.advance();

    const char* str_start = iter_.src() + start_quote_pos + 1;
    types::size_t len     = end_quote_pos - start_quote_pos - 1;

    return utils::unescape(str_start, len, iter_.arena());
}

bool
JsonValue::get_bool() NOEXCEPT {
    char c = iter_.peek_char();
    iter_.advance();
    return c == 't';
}

} // namespace zuu::ondemand