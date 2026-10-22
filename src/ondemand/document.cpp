#include <ze_json/ondemand/document.hpp>

namespace zuu::ondemand {

JsonValue
Document::get_root() NOEXCEPT {
    return JsonValue(iter_);
}

JsonObject
JsonValue::get_object() NOEXCEPT {
    return JsonObject(*iter_);
}

JsonArray
JsonValue::get_array() NOEXCEPT {
    return JsonArray(*iter_);
}

} // namespace zuu::ondemand