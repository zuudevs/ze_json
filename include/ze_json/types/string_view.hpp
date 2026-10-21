#pragma once

#include <ze_json/types/integral.hpp>

namespace zuu::types {

struct StringView {
    const char* data;
    size_t length;

    constexpr StringView(const char* d, size_t len) noexcept
        : data(d), length(len) {}

    constexpr bool
    operator==(const StringView& other) const noexcept {
        if (length != other.length)
            return false;
        for (size_t i = 0; i < length; ++i) {
            if (data[i] != other.data[i])
                return false;
        }
        return true;
    }
};

} // namespace zuu::types