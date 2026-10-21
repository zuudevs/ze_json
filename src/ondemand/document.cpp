#include <ze_json/ondemand/document.hpp>

namespace zuu::ondemand {

JsonValue
Document::get_root() NOEXCEPT {
    return JsonValue(iter_);
}

} // namespace zuu::ondemand