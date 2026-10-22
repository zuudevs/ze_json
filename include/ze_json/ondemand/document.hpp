#pragma once

#include <ze_json/ondemand/json_array.hpp>

namespace zuu::ondemand {

class Document {
  private:
    JsonIterator iter_;

  public:
    constexpr Document(
        const char* src, const types::uint64_t* tape, types::size_t tape_len,
        void* arena) NOEXCEPT : iter_(src, tape, tape_len, arena) {}

    JsonValue
    get_root() NOEXCEPT;
};

} // namespace zuu::ondemand