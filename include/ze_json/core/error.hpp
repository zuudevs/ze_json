#pragma once

#include <ze_json/types/integral.hpp>

namespace zuu::core {

enum class JsonErrc : types::uint8_t {
    None,
    EmptyJson,
    InvalidJson,
    UnexpectedQuote,
    InvalidQuote,
    InsufficientBuffer
};

} // namespace zuu::core