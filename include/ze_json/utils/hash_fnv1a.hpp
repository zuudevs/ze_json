#pragma once

#include <ze_json/types/integral.hpp>
#include <ze_json/types/string_view.hpp>

namespace zuu::utils {

// FNV-1a 64-bit: hash kunci untuk indeks pencarian field — tanpa tabel,
// deterministik lintas platform, dan murah untuk kunci pendek.
struct Hash {
    [[nodiscard]] static constexpr types::uint64_t
    fnv1a64(const types::StringView& key) NOEXCEPT {
        types::uint64_t h = 0xcbf29ce484222325_u64;
        for (types::size_t i = 0; i < key.length; ++i) {
            h ^= static_cast<types::uint64_t>(
                static_cast<types::uint8_t>(key.data[i]));
            h *= 0x0000010000000193_u64;
        }
        return h;
    }
};

} // namespace zuu::utils
