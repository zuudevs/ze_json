#pragma once

#include <ze_json/ondemand/document.hpp>
#include <ze_json/ondemand/json_array.hpp>
#include <ze_json/ondemand/json_field.hpp>
#include <ze_json/ondemand/json_iterator.hpp>
#include <ze_json/ondemand/json_object.hpp>
#include <ze_json/ondemand/json_value.hpp>

namespace zuu::ondemand {

inline JsonObject
JsonValue::get_object() noexcept {
    return JsonObject(iter_);
}

inline JsonArray
JsonValue::get_array() noexcept {
    return JsonArray(iter_);
}

} // namespace zuu::ondemand