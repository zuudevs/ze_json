#include <ze_json/ondemand/json_object.hpp>

namespace zuu::ondemand {

JsonValue
JsonObject::find_field(const types::StringView& target_key) NOEXCEPT {
    // Delegasi ke varian checked; hasil tak-ditemukan mengembalikan JsonValue
    // di posisi terminator (perilaku lama tetap terjaga).
    JsonValue result(iter_);
    find_field_checked(target_key, &result);
    return result;
}

bool
JsonObject::find_field_checked(
    const types::StringView& target_key, JsonValue* out) NOEXCEPT {
    types::size_t ignored = 0;
    return find_field_counted(target_key, out, ignored);
}

bool
JsonObject::find_field_counted(
    const types::StringView& target_key, JsonValue* out,
    types::size_t& members) NOEXCEPT {
    members = 0;
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
            *out = val;
            return true;
        } else {
            ++members; // Member dilalui tanpa hit; mahal pada object lebar.
            iter_.skip_child();
        }
    }
    return false;
}

} // namespace zuu::ondemand