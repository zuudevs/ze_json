#pragma once

#include <ze_json/ondemand/json_value.hpp>
#include <ze_json/types/string_view.hpp>

namespace zuu::ondemand {

class JsonObject {
  private:
    JsonIterator& iter_;

  public:
    constexpr explicit JsonObject(JsonIterator& iter) noexcept : iter_(iter) {
        iter_.advance();
    }

    JsonValue
    find_field(const types::StringView& target_key) noexcept {
        while (iter_.peek_char() != '}' && iter_.peek_char() != '\0') {
            if (iter_.peek_char() == ',') {
                iter_.advance();
            }

            JsonValue key_val(iter_);
            types::StringView current_key = key_val.get_string();

            if (iter_.peek_char() == ':') {
                iter_.advance();
            }

            JsonValue val(iter_);
            if (current_key == target_key) {
                return val;
            } else {
                iter_.skip_child();
            }
        }
        return JsonValue(iter_);
    }
};

} // namespace zuu::ondemand