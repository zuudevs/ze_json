#include <ze_json/scanner/scanner.hpp>
#include <ze_json/ze_json.hpp>

EXTERN_C_BEGIN

zuu::memory::Arena*
create_arena(void* memory_ptr, zuu::types::size_t total_capacity) NOEXCEPT {
    if (!memory_ptr || total_capacity <= sizeof(zuu::memory::Arena))
        [[unlikely]] {
        return nullptr;
    }

    void* buffer_start = static_cast<zuu::types::uint8_t*>(memory_ptr) +
                         sizeof(zuu::memory::Arena);
    zuu::types::size_t buffer_capacity =
        total_capacity - sizeof(zuu::memory::Arena);
    return new (memory_ptr) zuu::memory::Arena(buffer_start, buffer_capacity);
}

void
reset_arena(zuu::memory::Arena* arena) NOEXCEPT {
    if (arena) [[likely]] {
        arena->reset();
    }
}

void
destroy_arena(zuu::memory::Arena* arena) NOEXCEPT {
    if (arena) [[likely]] {
        arena->~Arena();
    }
}

zuu::types::size_t
get_arena_used_space(zuu::memory::Arena* arena) NOEXCEPT {
    return arena->get_used_space();
}

// Helper: membaca hasil Result tanpa menyentuh union yang tidak aktif
// (structured binding / get<1> dapat membaca member union yang salah).
namespace {
[[nodiscard]] zuu::types::int32_t
errc_to_status(
    zuu::types::Result<zuu::types::size_t, zuu::core::JsonErrc>& res) NOEXCEPT {
    return res.has_value() ? static_cast<zuu::types::int32_t>(zuu::api::OK)
                           : static_cast<zuu::types::int32_t>(res.error());
}
} // namespace

void
scanner8(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size,
    zuu::types::size_t& result, zuu::types::int32_t& error) NOEXCEPT {
    auto res = zuu::scanner::scanner8(src, src_size, dst, dst_size);
    error    = errc_to_status(res);
    if (res.has_value()) {
        result = res.value();
    }
}
#if __LITTLE_ENDIAN__ == 1
#if __IS_64_BIT__ == 1
void
scanner64(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size,
    zuu::types::size_t& result, zuu::types::int32_t& error) NOEXCEPT {
    auto res = zuu::scanner::scanner64(src, src_size, dst, dst_size);
    error    = errc_to_status(res);
    if (res.has_value()) {
        result = res.value();
    }
}
#elif __IS_64_BIT__ == 0
#else
void
scanner32(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size,
    zuu::types::size_t& result, zuu::types::int32_t& error) NOEXCEPT {
    auto res = zuu::scanner::scanner32(src, src_size, dst, dst_size);
    error    = errc_to_status(res);
    if (res.has_value()) {
        result = res.value();
    }
}
#endif // __IS_64_BIT
#endif // __LITTLE_ENDIAN__ == 1

// ============================================================================
//  IMPLEMENTASI SESSION API
// ============================================================================

zuu::api::Session*
create_session(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::uint64_t* RESTRICT tape, zuu::types::size_t tape_entries,
    zuu::memory::Arena* arena, zuu::types::int32_t& error) NOEXCEPT {
    if (!arena) [[unlikely]] {
        error = static_cast<zuu::types::int32_t>(zuu::api::INSUFFICIENT_BUFFER);
        return nullptr;
    }

    if (!src || !tape || src_size <= 1 || tape_entries <= 1) [[unlikely]] {
        error = static_cast<zuu::types::int32_t>(zuu::api::INVALID_JSON);
        return nullptr;
    }

    auto scan_res = zuu::scanner::scanner8(
        src, src_size,
        reinterpret_cast<zuu::types::size_t* const RESTRICT>(tape),
        tape_entries);
    if (!scan_res.has_value()) [[unlikely]] {
        error = errc_to_status(scan_res);
        return nullptr;
    }

    error = static_cast<zuu::types::int32_t>(zuu::api::OK);

    // Session dialokasikan dari arena; dinonaktifkan optionally dengan
    // reset_arena.
    auto* session = arena->make<zuu::api::Session>(
        src, static_cast<const zuu::types::uint64_t*>(tape), scan_res.value(),
        arena);
    if (!session) [[unlikely]] {
        error = static_cast<zuu::types::int32_t>(zuu::api::INSUFFICIENT_BUFFER);
        return nullptr;
    }

    return session;
}

zuu::types::int32_t
find_field(
    zuu::api::Session* session, const char* const RESTRICT key,
    zuu::types::size_t key_len) NOEXCEPT {
    if (!session || !key || key_len == 0) [[unlikely]] {
        return 0;
    }

    session->find(zuu::types::StringView(key, key_len));
    return session->found() ? 1 : 0;
}

zuu::types::size_t
array_size(zuu::api::Session* session) NOEXCEPT {
    return session ? session->array_size() : 0;
}

zuu::types::int32_t
array_next(zuu::api::Session* session) NOEXCEPT {
    return (session && session->array_next() == 1) ? 1 : 0;
}

// ============================================================================
//  IMPLEMENTASI PATH QUERY
// ============================================================================

zuu::types::int32_t
find_path(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len) NOEXCEPT {
    if (!session) [[unlikely]] {
        return static_cast<zuu::types::int32_t>(zuu::api::INVALID_PATH);
    }
    return session->find_path(path, path_len);
}

zuu::types::int64_t
get_int_at(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len, zuu::types::int32_t& error_out) NOEXCEPT {
    if (!session || !path || path_len == 0) [[unlikely]] {
        error_out = static_cast<zuu::types::int32_t>(zuu::api::INVALID_PATH);
        return 0;
    }

    const auto status = session->find_path(path, path_len);
    if (status != static_cast<zuu::types::int32_t>(zuu::api::OK)) [[likely]] {
        error_out = status;
        return 0;
    }

    if (session->type() != zuu::api::ValueType::Number) {
        error_out = static_cast<zuu::types::int32_t>(zuu::api::TYPE_MISMATCH);
        return 0;
    }

    error_out = static_cast<zuu::types::int32_t>(zuu::api::OK);
    return session->get_int();
}

double
get_double_at(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len, zuu::types::int32_t& error_out) NOEXCEPT {
    if (!session || !path || path_len == 0) [[unlikely]] {
        error_out = static_cast<zuu::types::int32_t>(zuu::api::INVALID_PATH);
        return 0.0;
    }

    const auto status = session->find_path(path, path_len);
    if (status != static_cast<zuu::types::int32_t>(zuu::api::OK)) [[likely]] {
        error_out = status;
        return 0.0;
    }

    if (session->type() != zuu::api::ValueType::Number) {
        error_out = static_cast<zuu::types::int32_t>(zuu::api::TYPE_MISMATCH);
        return 0.0;
    }

    error_out = static_cast<zuu::types::int32_t>(zuu::api::OK);
    return session->get_double();
}

zuu::types::int32_t
get_bool_at(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len, zuu::types::int32_t& error_out) NOEXCEPT {
    if (!session || !path || path_len == 0) [[unlikely]] {
        error_out = static_cast<zuu::types::int32_t>(zuu::api::INVALID_PATH);
        return 0;
    }

    const auto status = session->find_path(path, path_len);
    if (status != static_cast<zuu::types::int32_t>(zuu::api::OK)) [[likely]] {
        error_out = status;
        return 0;
    }

    if (session->type() != zuu::api::ValueType::Boolean) {
        error_out = static_cast<zuu::types::int32_t>(zuu::api::TYPE_MISMATCH);
        return 0;
    }

    error_out = static_cast<zuu::types::int32_t>(zuu::api::OK);
    return session->get_bool() ? 1 : 0;
}

void
get_string_at(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len, const char*& out_data,
    zuu::types::size_t& out_len, zuu::types::int32_t& error_out) NOEXCEPT {
    if (!session || !path || path_len == 0) [[unlikely]] {
        error_out = static_cast<zuu::types::int32_t>(zuu::api::INVALID_PATH);
        out_data  = nullptr;
        out_len   = 0;
        return;
    }
    out_data = nullptr;
    out_len  = 0;

    const auto status = session->find_path(path, path_len);
    if (status != static_cast<zuu::types::int32_t>(zuu::api::OK)) [[likely]] {
        error_out = status;
        return;
    }

    if (session->type() != zuu::api::ValueType::String) {
        error_out = static_cast<zuu::types::int32_t>(zuu::api::TYPE_MISMATCH);
        return;
    }

    const auto sv = session->get_string();
    out_data      = sv.data;
    out_len       = sv.length;
    error_out     = static_cast<zuu::types::int32_t>(zuu::api::OK);
}

zuu::types::int32_t
value_type(zuu::api::Session* session) NOEXCEPT {
    if (!session) [[unlikely]] {
        return -1;
    }
    return static_cast<zuu::types::int32_t>(session->type());
}

zuu::types::int64_t
get_int(zuu::api::Session* session) NOEXCEPT {
    return session ? session->get_int() : 0;
}

double
get_double(zuu::api::Session* session) NOEXCEPT {
    return session ? session->get_double() : 0.0;
}

zuu::types::int32_t
get_bool(zuu::api::Session* session) NOEXCEPT {
    return (session && session->get_bool()) ? 1 : 0;
}

void
get_string(
    zuu::api::Session* session, const char*& out_data,
    zuu::types::size_t& out_len) NOEXCEPT {
    if (!session) [[unlikely]] {
        out_data = nullptr;
        out_len  = 0;
        return;
    }

    auto sv  = session->get_string();
    out_data = sv.data;
    out_len  = sv.length;
}

void
destroy_session(zuu::api::Session* session) NOEXCEPT {
    // No-op: Session dibangun di atas arena (trivially destructible).
    // reset_arena yang mereklaim memori beserta seluruh string unescape.
    (void)session;
}

EXTERN_C_END