#pragma once

#include <ze_json/ondemand/json_object.hpp>

namespace zuu::ondemand {

class JsonArray {
private:
    JsonIterator& iter_;

public:
    constexpr explicit JsonArray(JsonIterator& iter) noexcept : iter_(iter) {
        iter_.advance(); // Lewati '['
    }
    
    // (Implementasi custom iterator C++ (begin/end) bisa ditambahkan di sini
    // agar bisa menggunakan rentang for-loop standar: for(auto val : array))
};

} // namespace zuu::ondemand