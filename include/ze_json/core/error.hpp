#pragma once

#include <ze_json/core/integral.hpp>

namespace zuu::core {

enum class JsonErrc : uint8_t {
  None,
  EmptyJson,
  InvalidJson,
  UnexpectedQuote,
  InvalidQuote,
  InsufficientBuffer,
  InvalidUtf8,
};

} // namespace zuu::core