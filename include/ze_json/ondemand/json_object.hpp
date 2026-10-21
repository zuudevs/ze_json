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
};

} // namespace zuu::ondemand