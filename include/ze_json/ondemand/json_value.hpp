#pragma once

#include <ze_json/ondemand/json_iterator.hpp>
#include <ze_json/ondemand/json_type.hpp>
#include <ze_json/types/string_view.hpp>
#include <ze_json/utils/number_parser.hpp>

namespace zuu::ondemand {

class JsonValue {
  private:
    JsonIterator& iter_;

  public:
    constexpr explicit JsonValue(JsonIterator& iter) NOEXCEPT : iter_(iter) {}

    [[nodiscard]] constexpr JsonType
    type() const NOEXCEPT {
        auto type = lookups::kStructural[static_cast<types::uint8_t>(
                        iter_.peek_char())] &
                    lookups::TYPE_MASK;
        return type == 0 ? JsonType::Number : static_cast<JsonType>(type);
    }

	[[nodiscard]] constexpr double get_double() NOEXCEPT {
        const char* p = iter_.src() + iter_.peek_position();
        iter_.advance();
        return utils::parse_double(p);
    }

    [[nodiscard]] constexpr types::int64_t get_int() NOEXCEPT {
        const char* p = iter_.src() + iter_.peek_position();
        iter_.advance();
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