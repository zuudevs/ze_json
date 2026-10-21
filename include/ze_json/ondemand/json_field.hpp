#pragma once

#include <ze_json/ondemand/json_value.hpp>
#include <ze_json/types/string_view.hpp>

namespace zuu::ondemand {

class JsonField {
  private:
    types::StringView key_;
    JsonValue value_;

  public:
    constexpr JsonField(types::StringView k, JsonValue v) NOEXCEPT : key_(k),
                                                                     value_(v) {
    }
    constexpr types::StringView
    key() const NOEXCEPT {
        return key_;
    }
    constexpr JsonValue
    value() NOEXCEPT {
        return value_;
    }
};

} // namespace zuu::ondemand