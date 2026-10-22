#pragma once

#include <ze_json/traits/is_same.hpp>
#include <ze_json/types/integral.hpp>
#include <ze_json/utils/move.hpp>

namespace zuu::types {

template <typename E> struct Unexpected {
    E error_val;
    constexpr explicit Unexpected(E e) noexcept : error_val(utils::move(e)) {}
};

template <typename R, typename E> class [[nodiscard]] Result {
  private:
    union {
        R m_result;
        E m_error;
    };
    bool m_is_error;

  public:
    using value_type = R;
    using error_type = E;

    constexpr Result(R result) noexcept
        : m_result(utils::move(result)), m_is_error(false) {}

    constexpr Result(E error) noexcept
        requires(!traits::is_same_v<R, E>)
        : m_error(utils::move(error)), m_is_error(true) {}

    constexpr Result(Unexpected<E> unexpected) noexcept
        : m_error(utils::move(unexpected.error_val)), m_is_error(true) {}

    constexpr ~Result() {
        if (m_is_error) {
            m_error.~E();
        } else {
            m_result.~R();
        }
    }

    constexpr Result(Result&& other) noexcept : m_is_error(other.m_is_error) {
        if (m_is_error) {
            ::new (static_cast<void*>(&m_error)) E(utils::move(other.m_error));
        } else {
            ::new (static_cast<void*>(&m_result))
                R(utils::move(other.m_result));
        }
    }

    constexpr Result&
    operator=(Result&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        if (m_is_error == other.m_is_error) {
            if (m_is_error) {
                m_error = utils::move(other.m_error);
            } else {
                m_result = utils::move(other.m_result);
            }
        } else {
            this->~Result();
            m_is_error = other.m_is_error;
            if (m_is_error) {
                ::new (static_cast<void*>(&m_error))
                    E(utils::move(other.m_error));
            } else {
                ::new (static_cast<void*>(&m_result))
                    R(utils::move(other.m_result));
            }
        }
        return *this;
    }

    Result(const Result&)
        requires(__is_trivially_copyable(R))
    = delete;
    Result&
    operator=(const Result&)
        requires(__is_trivially_copyable(R))
    = delete;

    [[nodiscard]] constexpr explicit
    operator bool() const noexcept {
        return !m_is_error;
    }
    [[nodiscard]] constexpr bool
    has_value() const noexcept {
        return !m_is_error;
    }
    [[nodiscard]] constexpr bool
    has_error() const noexcept {
        return m_is_error;
    }

    [[nodiscard]] constexpr R&
    value() & noexcept {
        return m_result;
    }
    [[nodiscard]] constexpr const R&
    value() const& noexcept {
        return m_result;
    }

    [[nodiscard]] constexpr E&
    error() & noexcept {
        return m_error;
    }
    [[nodiscard]] constexpr const E&
    error() const& noexcept {
        return m_error;
    }
};

template <size_t I, typename R, typename E>
[[nodiscard]] constexpr auto
get(Result<R, E>& res) noexcept {
    static_assert(I < 2, "Result only has 2 elements for structured binding");
    if constexpr (I == 0) {
        return res.has_value() ? &res.value() : nullptr;
    } else if constexpr (I == 1) {
        return res.has_error() ? &res.error() : nullptr;
    }
}

template <size_t I, typename R, typename E>
[[nodiscard]] constexpr auto
get(const Result<R, E>& res) noexcept {
    static_assert(I < 2, "Result only has 2 elements for structured binding");
    if constexpr (I == 0) {
        return res.has_value() ? &res.value() : nullptr;
    } else if constexpr (I == 1) {
        return res.has_error() ? &res.error() : nullptr;
    }
}

template <size_t I, typename R, typename E>
[[nodiscard]] constexpr auto
get(Result<R, E>&& res) noexcept {
    static_assert(I < 2, "Result only has 2 elements for structured binding");
    if constexpr (I == 0) {
        return res.has_value() ? &res.value() : nullptr;
    } else if constexpr (I == 1) {
        return res.has_error() ? &res.error() : nullptr;
    }
}

template <size_t I, typename R, typename E>
[[nodiscard]] constexpr auto
get(const Result<R, E>&& res) noexcept {
    static_assert(I < 2, "Result only has 2 elements for structured binding");
    if constexpr (I == 0) {
        return res.has_value() ? &res.value() : nullptr;
    } else if constexpr (I == 1) {
        return res.has_error() ? &res.error() : nullptr;
    }
}

} // namespace zuu::types

namespace std {
template <typename T> struct tuple_size;

template <zuu::types::size_t I, typename T> struct tuple_element;

template <typename R, typename E> struct tuple_size<zuu::types::Result<R, E>> {
    static constexpr zuu::types::size_t value = 2;
};

template <typename R, typename E>
struct tuple_element<0, zuu::types::Result<R, E>> {
    using type = R*;
};

template <typename R, typename E>
struct tuple_element<1, zuu::types::Result<R, E>> {
    using type = E*;
};

template <typename R, typename E>
struct tuple_element<0, const zuu::types::Result<R, E>> {
    using type = const R*;
};

template <typename R, typename E>
struct tuple_element<1, const zuu::types::Result<R, E>> {
    using type = const E*;
};
} // namespace std