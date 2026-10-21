#pragma once

#include <ze_json/ondemand/parser.hpp>

namespace zuu::ondemand {

class Document {
private:
    JsonIterator iter_;

public:
    constexpr Document(const char* src, const types::uint64_t* tape, types::size_t tape_len) noexcept
        : iter_(src, tape, tape_len) {}

    JsonValue get_root() noexcept {
        return JsonValue(iter_);
    }
};

} // namespace zuu::ondemand