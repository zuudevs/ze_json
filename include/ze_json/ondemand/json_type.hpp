#pragma once

#include <ze_json/types/integral.hpp>

namespace zuu::ondemand {

enum class JsonType : types::uint8_t {
    None    = 0, // 0b000
    Array   = 1, // 0b001
    Object  = 2, // 0b010
    String  = 3, // 0b011
    Boolean = 4, // 0b100
    Null    = 5, // 0b101
    Number  = 6, // 0b110
    Other   = 7  // 0b111 (Digunakan untuk Koma dan Titik Dua)
};

} // namespace zuu::ondemand