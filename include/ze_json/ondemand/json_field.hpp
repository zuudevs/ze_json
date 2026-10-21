#pragma once

#include <ze_json/ondemand/json_value.hpp>
#include <ze_json/types/string_view.hpp>

namespace zuu::ondemand {

class JsonField {
private:
    types::StringView key_;
    JsonValue value_;

public:
    constexpr JsonField(types::StringView k, JsonValue v) noexcept : key_(k), value_(v) {}
    constexpr types::StringView key() const noexcept { return key_; }
    constexpr JsonValue value() noexcept { return value_; }
};

} // namespace zuu::ondemand