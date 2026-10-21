#pragma once

#include <ze_json/types/integral.hpp>

namespace zuu::types {

template <typename T, size_t N, size_t A = alignof(T)> class Array {
  private:
    alignas(A) T data[N]{};

  public:
    constexpr Array() noexcept             = default;
    constexpr Array(const Array&) noexcept = default;
    constexpr Array(Array&&) noexcept      = default;
    constexpr Array&
    operator=(const Array&) noexcept = default;
    constexpr Array&
    operator=(Array&&) noexcept = default;

    template <typename G, size_t C>
        requires(__is_convertible(G, T) && N >= C)
    constexpr Array(const G (&arr)[C]) noexcept {
        for (size_t i = 0; i < C; ++i) {
            data[i] = static_cast<T>(arr[i]);
        }
    }

    [[nodiscard]] constexpr T&
    operator[](size_t idx) noexcept {
        return data[idx];
    }

    [[nodiscard]] constexpr const T&
    operator[](size_t idx) const noexcept {
        return data[idx];
    }

    [[nodiscard]] constexpr T
    get_or(size_t idx, T default_value = T{}) const noexcept {
        return (idx < N) ? data[idx] : default_value;
    }

    [[nodiscard]] constexpr T*
    get(size_t idx) noexcept {
        return (idx < N) ? &data[idx] : nullptr;
    }

    [[nodiscard]] constexpr const T*
    get(size_t idx) const noexcept {
        return (idx < N) ? &data[idx] : nullptr;
    }

    [[nodiscard]] constexpr T*
    begin() noexcept {
        return data;
    }
    [[nodiscard]] constexpr const T*
    begin() const noexcept {
        return data;
    }

    [[nodiscard]] constexpr T*
    end() noexcept {
        return data + N;
    }
    [[nodiscard]] constexpr const T*
    end() const noexcept {
        return data + N;
    }

    [[nodiscard]] constexpr size_t
    size() const noexcept {
        return N;
    }
    [[nodiscard]] constexpr bool
    empty() const noexcept {
        return N == 0;
    }
};

} // namespace zuu::types