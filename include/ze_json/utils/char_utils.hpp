#pragma once

#include <array>
#ifdef UNABLE_AVX512
#ifdef __AVX512BW__
#undef __AVX512BW__
#endif // __AVX512BW__
#endif // UNABLE_AVX512

#include <macros.hpp>

#define SWAR
#define SIMD

#include <concepts>

#ifdef SWAR
#include <cstdint>
#endif // SWAR

#ifdef SIMD
#if defined(__AVX2__) || defined(__AVX512BW__) || defined(__SSE2__)
#include <emmintrin.h>
#include <immintrin.h>
#include <x86intrin.h>
#endif // __AVX2__ || __AVX512BW__ || __SSE2__
#endif // SIMD

namespace zuu {

#ifdef SWAR

// 0 = skip (whitespace, angka, dll)
// 1 = struktur (harus dicatat ke tape)
// 2 = string quote awal (masuk fase SWAR)
[[nodiscard]] static consteval std::array<uint8_t, 256>
build_structural_map() NOEXCEPT {
    std::array<uint8_t, 256> map{};
    map['\"'] = 2;
    map[','] = map[':'] = map['{'] = map['}'] = map['['] = map[']'] = map['t'] =
        map['n'] = map['f'] = 1;
    return map;
}

constexpr auto kStructuralMap = build_structural_map();

/**
 * @brief Checks if a character is an alphabetical letter (A-Z or a-z).
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. lower = ch | mask (Case Normalization via bitwise OR)
 * 2. delta = lower - 'a' (Unsigned subtraction)
 * 3. delta < 26 (Range check)
 *
 * **Techniques Used:**
 * - Unsigned Subtraction Range Check
 * - Branchless Case Normalization / Case Folding
 * - Bitwise Range Reduction Trick
 *
 * @param ch The character to check.
 * @return uint8_t Returns 1 if alphabetical, 0 otherwise.
 */
[[nodiscard]] FORCE_INLINE constexpr uint8_t
is_alphabet(uint8_t ch) NOEXCEPT {
    constexpr uint8_t MASK           = 0x20;
    constexpr uint8_t ALPHABET_COUNT = 26;
    return static_cast<uint8_t>((ch | MASK) - 'a') < ALPHABET_COUNT;
}

/**
 * @brief Checks if a character is a numeric digit (0-9).
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. delta = ch - '0' (Direct subtraction)
 * 2. delta < 10 (Range check)
 *
 * **Techniques Used:**
 * - Unsigned Subtraction Range Check
 *
 * @param ch The character to check.
 * @return uint8_t Returns 1 if numeric digit, 0 otherwise.
 */
[[nodiscard]] FORCE_INLINE constexpr uint8_t
is_number(uint8_t ch) NOEXCEPT {
    return static_cast<uint8_t>(ch - '0') < 10;
}

/**
 * @brief Checks if a character is a whitespace (space, tab, newline, etc.).
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. Early branch check for regular space (' ') compile time.
 * 2. delta = ch - '\t' (Unsigned subtraction for control characters)
 * 3. delta < 5 (Covers ASCII 9 to 13: \\t, \\n, \\v, \\f, \\r)
 *
 * **Techniques Used:**
 * - Hybrid Branchless Range Check
 *
 * @param ch The character to check.
 * @return uint8_t Returns 1 if whitespace, 0 otherwise.
 */
[[nodiscard]] FORCE_INLINE constexpr uint8_t
is_whitespace(uint8_t ch) NOEXCEPT {
    return ch == ' ' ? 1 : static_cast<uint8_t>(ch - '\t') < 5;
}

namespace __detail {

template <typename T, T Val, size_t I> struct repeat_bytes_impl {
    static constexpr T value =
        (Val << (I * 8)) | repeat_bytes_impl<T, Val, I - 1>::value;
};

template <typename T, T Val> struct repeat_bytes_impl<T, Val, 0> {
    static constexpr T value = Val;
};

} // namespace __detail

template <std::integral T, auto Val> struct repeat_bytes {
    static constexpr T value = __detail::
        repeat_bytes_impl<T, static_cast<T>(Val), sizeof(T) - 1>::value;
};

template <std::integral T, auto Val>
constexpr T repeat_bytes_v = repeat_bytes<T, Val>::value;

/**
 * @brief Checks and extracts alphabetical character flags across a 64-bit word
 * using SWAR.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. normalized_char = value & repeat_bytes_v<uint64_t, 0x5F> (Clear bit 5 for
 * case normalization & high bits)
 * 2. lower_bound = normalized_char + repeat_bytes_v<uint64_t, 127 - 'A' + 1>
 * (Underflow detection for range start)
 * 3. upper_bound = ~(normalized_char + repeat_bytes_v<uint64_t, 127 - 'Z'>)
 * (Overflow detection for range end)
 * 4. valid_ascii = ~value (Ensures MSB is 0, keeping checks strictly within
 * 7-bit ASCII)
 * 5. Returns bitwise combination filtered by 0x80 flag per byte.
 *
 * **Techniques Used:**
 * - SWAR (Simultaneous Word Within A Register) Parallel Processing
 * - Bitwise Range Reduction Trick
 * - Parallel Underflow/Overflow Flag Extraction
 *
 * @param value The 64-bit integer containing up to 8 packed characters.
 * @return uint64_t Packed flags where each byte has its MSB set if the
 * character is an alphabet.
 */
[[nodiscard]] FORCE_INLINE constexpr uint64_t
get_alphabet_flags64(uint64_t value) NOEXCEPT {
    uint64_t normalized_char = value & repeat_bytes_v<uint64_t, 0x5F>;
    uint64_t lower_bound =
        normalized_char + repeat_bytes_v<uint64_t, 127 - 'A' + 1>;
    uint64_t upper_bound =
        ~(normalized_char + repeat_bytes_v<uint64_t, 127 - 'Z'>);
    uint64_t valid_ascii = ~value;

    return (
        lower_bound & upper_bound & valid_ascii &
        repeat_bytes_v<uint64_t, 0x80>);
}

/**
 * @brief Checks and extracts numeric digit flags across a 64-bit word using
 * SWAR.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. cleared_msb = value & repeat_bytes_v<uint64_t, 0x7F> (Clear MSB for safe
 * arithmetic)
 * 2. lower_bound = cleared_msb + repeat_bytes_v<uint64_t, 127 - '0' + 1>
 * (Underflow detection for '0')
 * 3. upper_bound = ~(cleared_msb + repeat_bytes_v<uint64_t, 127 - '9'>)
 * (Overflow detection for '9')
 * 4. valid_ascii = ~value (Ensures valid 7-bit ASCII range)
 * 5. Returns bitwise combination filtered by 0x80 flag per byte.
 *
 * **Techniques Used:**
 * - SWAR Parallel Processing
 * - Unsigned Subtraction Range Check across 8 bytes
 *
 * @param value The 64-bit integer containing up to 8 packed characters.
 * @return uint64_t Packed flags where each byte has its MSB set if the
 * character is a number.
 */
[[nodiscard]] FORCE_INLINE constexpr uint64_t
get_number_flags64(uint64_t value) NOEXCEPT {
    uint64_t cleared_msb = value & repeat_bytes_v<uint64_t, 0x7F>;
    uint64_t lower_bound =
        cleared_msb + repeat_bytes_v<uint64_t, 127 - '0' + 1>;
    uint64_t upper_bound = ~(cleared_msb + repeat_bytes_v<uint64_t, 127 - '9'>);
    uint64_t valid_ascii = ~value;

    return (
        lower_bound & upper_bound & valid_ascii &
        repeat_bytes_v<uint64_t, 0x80>);
}

/**
 * @brief Checks and extracts whitespace flags across a 64-bit word using SWAR.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. xor_space = value ^ repeat_bytes_v<uint64_t, ' '> (Equality check for
 * regular space)
 * 2. is_space = Alan Mycroft's Null-Byte Detection trick
 * 3. cleared_msb & bounds = checks control whitespaces ('\t' to '\r')
 * 4. Combines space and control whitespace flags via bitwise OR.
 *
 * **Techniques Used:**
 * - SWAR Parallel Processing
 * - Alan Mycroft's Null-Byte Detection Trick
 * - Hybrid Control-Character Range Check
 *
 * @param value The 64-bit integer containing up to 8 packed characters.
 * @return uint64_t Packed flags where each byte has its MSB set if the
 * character is whitespace.
 */
[[nodiscard]] FORCE_INLINE constexpr uint64_t
get_whitespace_flags64(uint64_t value) NOEXCEPT {
    uint64_t xor_space = value ^ repeat_bytes_v<uint64_t, ' '>;
    uint64_t is_space =
        (xor_space - repeat_bytes_v<uint64_t, 0x01>)&~xor_space &
        repeat_bytes_v<uint64_t, 0x80>;

    uint64_t cleared_msb = value & repeat_bytes_v<uint64_t, 0x7F>;
    uint64_t lower_bound =
        cleared_msb + repeat_bytes_v<uint64_t, 127 - '\t' + 1>;
    uint64_t upper_bound =
        ~(cleared_msb + repeat_bytes_v<uint64_t, 127 - '\r'>);
    uint64_t valid_ascii = ~value;
    uint64_t is_ctrl_ws  = lower_bound & upper_bound & valid_ascii;

    return ((is_space | is_ctrl_ws) & repeat_bytes_v<uint64_t, 0x80>);
}

/**
 * @brief Checks if all 8 characters in a 64-bit word are alphabetical letters.
 * @details Evaluates SWAR alphabet flags against the expected full-mask
 * sentinel value.
 * @param value The 64-bit integer containing packed characters.
 * @return bool True if all bytes are alphabetical, false otherwise.
 */
[[nodiscard]] FORCE_INLINE constexpr bool
is_all_alphabet64(uint64_t value) NOEXCEPT {
    return get_alphabet_flags64(value) == repeat_bytes_v<uint64_t, 0x80>;
}

/**
 * @brief Checks if all 8 characters in a 64-bit word are numeric digits.
 * @details Evaluates SWAR number flags against the expected full-mask sentinel
 * value.
 * @param value The 64-bit integer containing packed characters.
 * @return bool True if all bytes are numeric, false otherwise.
 */
[[nodiscard]] FORCE_INLINE constexpr bool
is_all_number64(uint64_t value) NOEXCEPT {
    return get_number_flags64(value) == repeat_bytes_v<uint64_t, 0x80>;
}

/**
 * @brief Checks if all 8 characters in a 64-bit word are whitespace characters.
 * @details Evaluates SWAR whitespace flags against the expected full-mask
 * sentinel value.
 * @param value The 64-bit integer containing packed characters.
 * @return bool True if all bytes are whitespace, false otherwise.
 */
[[nodiscard]] FORCE_INLINE constexpr bool
is_all_whitespace64(uint64_t value) NOEXCEPT {
    return get_whitespace_flags64(value) == repeat_bytes_v<uint64_t, 0x80>;
}

/**
 * @brief Checks if any character in a 64-bit word is an alphabetical letter.
 * @details Checks if SWAR alphabet flags contain any non-zero byte.
 * @param value The 64-bit integer containing packed characters.
 * @return bool True if at least one byte is alphabetical, false otherwise.
 */
[[nodiscard]] FORCE_INLINE constexpr bool
has_alphabet64(uint64_t value) NOEXCEPT {
    return get_alphabet_flags64(value) != 0;
}

/**
 * @brief Checks if any character in a 64-bit word is a numeric digit.
 * @details Checks if SWAR number flags contain any non-zero byte.
 * @param value The 64-bit integer containing packed characters.
 * @return bool True if at least one byte is numeric, false otherwise.
 */
[[nodiscard]] FORCE_INLINE constexpr bool
has_number64(uint64_t value) NOEXCEPT {
    return get_number_flags64(value) != 0;
}

/**
 * @brief Checks if any character in a 64-bit word is a whitespace character.
 * @details Checks if SWAR whitespace flags contain any non-zero byte.
 * @param value The 64-bit integer containing packed characters.
 * @return bool True if at least one byte is whitespace, false otherwise.
 */
[[nodiscard]] FORCE_INLINE constexpr bool
has_whitespace64(uint64_t value) NOEXCEPT {
    return get_whitespace_flags64(value) != 0;
}

#endif // SWAR

#ifdef SIMD
#ifdef __AVX2__

/**
 * @brief Checks and extracts alphabetical character flags across a 128-bit
 * vector using SSE/AVX.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. normalized_char = value & 0xDF (Branchless case folding to uppercase)
 * 2. gt64 = normalized_char > 0x40 (Strictly greater than 'A' - 1)
 * 3. lt91 = normalized_char < 0x5B (Strictly less than 'Z' + 1)
 * 4. return gt64 & lt91 (Combines conditions via bitwise AND)
 *
 * **Techniques Used:**
 * - SIMD Vectorized Processing
 * - Branchless Case Normalization
 * - Implicit Safe-Signed Comparison (Non-ASCII >127 maps to negative values,
 * automatically failing >0x40 check)
 *
 * @param value The 128-bit SIMD register containing 16 packed characters.
 * @return __m128i Vector containing 0xFF in bytes that are alphabetical, 0x00
 * otherwise.
 */
[[nodiscard]] FORCE_INLINE __m128i
get_alphabet_flags128(__m128i value) NOEXCEPT {
    const auto k128xDF = _mm_set1_epi8(0xDF);
    const auto k128x40 = _mm_set1_epi8(0x40); // A - 1
    const auto k128x5B = _mm_set1_epi8(0x5B); // Z + 1

    auto normalized_char = _mm_and_si128(value, k128xDF);
    auto gt64            = _mm_cmpgt_epi8(normalized_char, k128x40);
    auto lt91            = _mm_cmpgt_epi8(k128x5B, normalized_char);

    return _mm_and_si128(gt64, lt91);
}

/**
 * @brief Checks and extracts numeric digit flags across a 128-bit vector using
 * SSE/AVX.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. gt47 = value > 0x2F (Strictly greater than '0' - 1)
 * 2. lt58 = value < 0x3A (Strictly less than '9' + 1)
 * 3. return gt47 & lt58 (Combines conditions via bitwise AND)
 *
 * **Techniques Used:**
 * - SIMD Vectorized Processing
 * - Implicit Safe-Signed Comparison
 *
 * @param value The 128-bit SIMD register containing 16 packed characters.
 * @return __m128i Vector containing 0xFF in bytes that are numbers, 0x00
 * otherwise.
 */
[[nodiscard]] FORCE_INLINE __m128i
get_number_flags128(__m128i value) NOEXCEPT {
    const auto k128x2F = _mm_set1_epi8(0x2F); // 0 - 1
    const auto k128x3A = _mm_set1_epi8(0x3A); // 9 + 1

    auto gt47 = _mm_cmpgt_epi8(value, k128x2F);
    auto lt58 = _mm_cmpgt_epi8(k128x3A, value);

    return _mm_and_si128(gt47, lt58);
}

/**
 * @brief Checks and extracts whitespace flags across a 128-bit vector using
 * SSE/AVX.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. Broadcast LUT containing valid whitespace characters mapped to their
 * lower-nibble indices.
 * 2. Perform `_mm_shuffle_epi8` (PSHUFB) using the input vector as indices.
 * 3. Compare the original vector with the shuffled expected vector.
 *
 * **Techniques Used:**
 * - SIMD Vectorized Processing
 * - PSHUFB (Parallel Shuffle) LUT Trick
 * - Branchless Set Classification
 *
 * @param value The 128-bit SIMD register containing 16 packed characters.
 * @return __m128i Vector containing 0xFF in bytes that are whitespace, 0x00
 * otherwise.
 */
[[nodiscard]] FORCE_INLINE __m128i
get_whitespace_flags128(__m128i value) NOEXCEPT {
    const auto k128WsLut = _mm_setr_epi8(
        0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x0A, 0x00,
        0x00, 0x0D, 0x00, 0x00);
    return _mm_cmpeq_epi8(_mm_shuffle_epi8(k128WsLut, value), value);
}

/**
 * @brief Checks if all 16 characters in a 128-bit vector are alphabetical
 * letters.
 * @details Extracts the MSB of each byte into an integer mask and compares
 * against 0xFFFF.
 * @param value The 128-bit SIMD register containing packed characters.
 * @return bool True if all bytes are alphabetical, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
is_all_alphabet128(__m128i value) NOEXCEPT {
    return _mm_movemask_epi8(get_alphabet_flags128(value)) ==
           repeat_bytes_v<uint16_t, 0xFF>;
}

/**
 * @brief Checks if all 16 characters in a 128-bit vector are numeric digits.
 * @details Extracts the MSB of each byte into an integer mask and compares
 * against 0xFFFF.
 * @param value The 128-bit SIMD register containing packed characters.
 * @return bool True if all bytes are numeric, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
is_all_number128(__m128i value) NOEXCEPT {
    return _mm_movemask_epi8(get_number_flags128(value)) ==
           repeat_bytes_v<uint16_t, 0xFF>;
}

/**
 * @brief Checks if all 16 characters in a 128-bit vector are whitespace
 * characters.
 * @details Extracts the MSB of each byte into an integer mask and compares
 * against 0xFFFF.
 * @param value The 128-bit SIMD register containing packed characters.
 * @return bool True if all bytes are whitespace, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
is_all_whitespace128(__m128i value) NOEXCEPT {
    return _mm_movemask_epi8(get_whitespace_flags128(value)) ==
           repeat_bytes_v<uint16_t, 0xFF>;
}

/**
 * @brief Checks if any character in a 128-bit vector is an alphabetical letter.
 * @details Uses `vptest` (`_mm_testz_si128`) for instantaneous zero-detection.
 * @param value The 128-bit SIMD register containing packed characters.
 * @return bool True if at least one byte is alphabetical, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
has_alphabet128(__m128i value) NOEXCEPT {
    __m128i flags = get_alphabet_flags128(value);
    return !_mm_testz_si128(flags, flags);
}

/**
 * @brief Checks if any character in a 128-bit vector is a numeric digit.
 * @details Uses `vptest` (`_mm_testz_si128`) for instantaneous zero-detection.
 * @param value The 128-bit SIMD register containing packed characters.
 * @return bool True if at least one byte is numeric, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
has_number128(__m128i value) NOEXCEPT {
    __m128i flags = get_number_flags128(value);
    return !_mm_testz_si128(flags, flags);
}

/**
 * @brief Checks if any character in a 128-bit vector is a whitespace character.
 * @details Uses `vptest` (`_mm_testz_si128`) for instantaneous zero-detection.
 * @param value The 128-bit SIMD register containing packed characters.
 * @return bool True if at least one byte is whitespace, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
has_whitespace128(__m128i value) NOEXCEPT {
    __m128i flags = get_whitespace_flags128(value);
    return !_mm_testz_si128(flags, flags);
}

/**
 * @brief Checks and extracts alphabetical character flags across a 256-bit
 * vector using AVX2.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. normalized_char = value & 0xDF (Branchless case folding to uppercase)
 * 2. gt64 = normalized_char > 0x40 (Strictly greater than 'A' - 1)
 * 3. lt91 = normalized_char < 0x5B (Strictly less than 'Z' + 1)
 * 4. return gt64 & lt91 (Combines conditions via bitwise AND)
 *
 * **Techniques Used:**
 * - SIMD Vectorized Processing
 * - Branchless Case Normalization
 * - Implicit Safe-Signed Comparison
 *
 * @param value The 256-bit SIMD register containing 32 packed characters.
 * @return __m256i Vector containing 0xFF in bytes that are alphabetical, 0x00
 * otherwise.
 */
[[nodiscard]] FORCE_INLINE __m256i
get_alphabet_flags256(__m256i value) NOEXCEPT {
    const auto k256x40 = _mm256_set1_epi8(0x40); // A - 1
    const auto k256x5B = _mm256_set1_epi8(0x5B); // Z + 1
    const auto k256xDF = _mm256_set1_epi8(0xDF);

    auto normalized_char = _mm256_and_si256(value, k256xDF);
    auto gt64            = _mm256_cmpgt_epi8(normalized_char, k256x40);
    auto lt91            = _mm256_cmpgt_epi8(k256x5B, normalized_char);

    return _mm256_and_si256(gt64, lt91);
}

/**
 * @brief Checks and extracts numeric digit flags across a 256-bit vector using
 * AVX2.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. gt47 = value > 0x2F (Strictly greater than '0' - 1)
 * 2. lt58 = value < 0x3A (Strictly less than '9' + 1)
 * 3. return gt47 & lt58 (Combines conditions via bitwise AND)
 *
 * **Techniques Used:**
 * - SIMD Vectorized Processing
 * - Implicit Safe-Signed Comparison
 *
 * @param value The 256-bit SIMD register containing 32 packed characters.
 * @return __m256i Vector containing 0xFF in bytes that are numbers, 0x00
 * otherwise.
 */
[[nodiscard]] FORCE_INLINE __m256i
get_number_flags256(__m256i value) NOEXCEPT {
    const auto k256x2F = _mm256_set1_epi8(0x2F); // 0 - 1
    const auto k256x3A = _mm256_set1_epi8(0x3A); // 9 + 1

    auto gt47 = _mm256_cmpgt_epi8(value, k256x2F);
    auto lt58 = _mm256_cmpgt_epi8(k256x3A, value);

    return _mm256_and_si256(gt47, lt58);
}

/**
 * @brief Checks and extracts whitespace flags across a 256-bit vector using
 * AVX2.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. Broadcast LUT containing valid whitespace characters mapped to their
 * lower-nibble indices.
 * 2. Perform `_mm256_shuffle_epi8` (PSHUFB) across both 128-bit lanes using the
 * input vector as indices.
 * 3. Compare the original vector with the shuffled expected vector.
 *
 * **Techniques Used:**
 * - SIMD Vectorized Processing
 * - PSHUFB (Parallel Shuffle) LUT Trick
 * - Branchless Set Classification
 *
 * @param value The 256-bit SIMD register containing 32 packed characters.
 * @return __m256i Vector containing 0xFF in bytes that are whitespace, 0x00
 * otherwise.
 */
[[nodiscard]] FORCE_INLINE __m256i
get_whitespace_flags256(__m256i value) NOEXCEPT {
    const auto k256WsLut = _mm256_setr_epi8(
        0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x0A, 0x00,
        0x00, 0x0D, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x09, 0x0A, 0x00, 0x00, 0x0D, 0x00, 0x00);
    return _mm256_cmpeq_epi8(_mm256_shuffle_epi8(k256WsLut, value), value);
}

/**
 * @brief Checks if all 32 characters in a 256-bit vector are alphabetical
 * letters.
 * @details Extracts the MSB of each byte into an integer mask and compares
 * against 0xFFFFFFFF.
 * @param value The 256-bit SIMD register containing packed characters.
 * @return bool True if all bytes are alphabetical, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
is_all_alphabet256(__m256i value) NOEXCEPT {
    return _mm256_movemask_epi8(get_alphabet_flags256(value)) ==
           repeat_bytes_v<int32_t, 0xFF>;
}

/**
 * @brief Checks if all 32 characters in a 256-bit vector are numeric digits.
 * @details Extracts the MSB of each byte into an integer mask and compares
 * against 0xFFFFFFFF.
 * @param value The 256-bit SIMD register containing packed characters.
 * @return bool True if all bytes are numeric, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
is_all_number256(__m256i value) NOEXCEPT {
    return _mm256_movemask_epi8(get_number_flags256(value)) ==
           repeat_bytes_v<int32_t, 0xFF>;
}

/**
 * @brief Checks if all 32 characters in a 256-bit vector are whitespace
 * characters.
 * @details Extracts the MSB of each byte into an integer mask and compares
 * against 0xFFFFFFFF.
 * @param value The 256-bit SIMD register containing packed characters.
 * @return bool True if all bytes are whitespace, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
is_all_whitespace256(__m256i value) NOEXCEPT {
    return _mm256_movemask_epi8(get_whitespace_flags256(value)) ==
           repeat_bytes_v<int32_t, 0xFF>;
}

/**
 * @brief Checks if any character in a 256-bit vector is an alphabetical letter.
 * @details Uses `vptest` (`_mm256_testz_si256`) for instantaneous
 * zero-detection.
 * @param value The 256-bit SIMD register containing packed characters.
 * @return bool True if at least one byte is alphabetical, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
has_alphabet256(__m256i value) NOEXCEPT {
    __m256i flags = get_alphabet_flags256(value);
    return !_mm256_testz_si256(flags, flags);
}

/**
 * @brief Checks if any character in a 256-bit vector is a numeric digit.
 * @details Uses `vptest` (`_mm256_testz_si256`) for instantaneous
 * zero-detection.
 * @param value The 256-bit SIMD register containing packed characters.
 * @return bool True if at least one byte is numeric, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
has_number256(__m256i value) NOEXCEPT {
    __m256i flags = get_number_flags256(value);
    return !_mm256_testz_si256(flags, flags);
}

/**
 * @brief Checks if any character in a 256-bit vector is a whitespace character.
 * @details Uses `vptest` (`_mm256_testz_si256`) for instantaneous
 * zero-detection.
 * @param value The 256-bit SIMD register containing packed characters.
 * @return bool True if at least one byte is whitespace, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
has_whitespace256(__m256i value) NOEXCEPT {
    __m256i flags = get_whitespace_flags256(value);
    return !_mm256_testz_si256(flags, flags);
}

#endif // __AVX2__

#ifdef __AVX512BW__

/**
 * @brief Checks and extracts alphabetical character flags across a 512-bit
 * vector using AVX-512BW.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. normalized_char = value & 0xDF (Branchless case folding to uppercase)
 * 2. Return evaluation of `normalized_char > 0x40` & `normalized_char < 0x5B`
 *
 * **Techniques Used:**
 * - AVX-512 Native OP-Mask Registers (k1-k7)
 * - SIMD Vectorized Processing
 * - Implicit Safe-Signed Comparison
 *
 * @param value The 512-bit SIMD register containing 64 packed characters.
 * @return __mmask64 64-bit mask where each bit represents if the corresponding
 * byte is an alphabet.
 */
[[nodiscard]] FORCE_INLINE auto
get_alphabet_flags512(__m512i value) NOEXCEPT {
    const auto k512x40 = _mm512_set1_epi8(0x40); // A - 1
    const auto k512x5B = _mm512_set1_epi8(0x5B); // Z + 1
    const auto k512xDF = _mm512_set1_epi8(0xDF);

    auto normalized_char = _mm512_and_si512(value, k512xDF);
    return _mm512_cmpgt_epi8_mask(normalized_char, k512x40) &
           _mm512_cmpgt_epi8_mask(k512x5B, normalized_char);
}

/**
 * @brief Checks and extracts numeric digit flags across a 512-bit vector using
 * AVX-512BW.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. Return evaluation of `value > 0x2F` & `value < 0x3A` mapped directly to
 * OP-Mask.
 *
 * **Techniques Used:**
 * - AVX-512 Native OP-Mask Registers (k1-k7)
 * - SIMD Vectorized Processing
 * - Implicit Safe-Signed Comparison
 *
 * @param value The 512-bit SIMD register containing 64 packed characters.
 * @return __mmask64 64-bit mask where each bit represents if the corresponding
 * byte is a number.
 */
[[nodiscard]] FORCE_INLINE auto
get_number_flags512(__m512i value) NOEXCEPT {
    const auto k512x2F = _mm512_set1_epi8(0x2F); // 0 - 1
    const auto k512x3A = _mm512_set1_epi8(0x3A); // 9 + 1

    return _mm512_cmpgt_epi8_mask(value, k512x2F) &
           _mm512_cmpgt_epi8_mask(k512x3A, value);
}

/**
 * @brief Checks and extracts whitespace flags across a 512-bit vector using
 * AVX-512BW.
 * @details
 * **Step-by-step Dependency Chain:**
 * 1. Broadcast LUT containing valid whitespace characters mapped to their
 * lower-nibble indices.
 * 2. Perform `_mm512_shuffle_epi8` (PSHUFB) using the input vector as indices.
 * 3. Compare the original vector with the shuffled expected vector directly
 * into an OP-Mask.
 *
 * **Techniques Used:**
 * - AVX-512 Native OP-Mask Registers (k1-k7)
 * - PSHUFB (Parallel Shuffle) LUT Trick
 *
 * @param value The 512-bit SIMD register containing 64 packed characters.
 * @return __mmask64 64-bit mask where each bit represents if the corresponding
 * byte is whitespace.
 */
[[nodiscard]] FORCE_INLINE auto
get_whitespace_flags512(__m512i value) NOEXCEPT {
    const auto k512WsLut = _mm512_broadcast_i32x4(_mm_setr_epi8(
        0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x0A, 0x00,
        0x00, 0x0D, 0x00, 0x00));
    return _mm512_cmpeq_epi8_mask(_mm512_shuffle_epi8(k256WsLut, value), value);
}

/**
 * @brief Checks if all 64 characters in a 512-bit vector are alphabetical
 * letters.
 * @details Evaluates the 64-bit AVX-512 mask against a fully set mask (~0ull).
 * @param value The 512-bit SIMD register containing packed characters.
 * @return bool True if all bytes are alphabetical, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
is_all_alphabet512(__m512i value) NOEXCEPT {
    return get_alphabet_flags512(value) == ~0ull;
}

/**
 * @brief Checks if all 64 characters in a 512-bit vector are numeric digits.
 * @details Evaluates the 64-bit AVX-512 mask against a fully set mask (~0ull).
 * @param value The 512-bit SIMD register containing packed characters.
 * @return bool True if all bytes are numeric, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
is_all_number512(__m512i value) NOEXCEPT {
    return get_number_flags512(value) == ~0ull;
}

/**
 * @brief Checks if all 64 characters in a 512-bit vector are whitespace
 * characters.
 * @details Evaluates the 64-bit AVX-512 mask against a fully set mask (~0ull).
 * @param value The 512-bit SIMD register containing packed characters.
 * @return bool True if all bytes are whitespace, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
is_all_whitespace512(__m512i value) NOEXCEPT {
    return get_whitespace_flags512(value) == ~0ull;
}

/**
 * @brief Checks if any character in a 512-bit vector is an alphabetical letter.
 * @details Evaluates if the 64-bit AVX-512 mask contains any set bits.
 * @param value The 512-bit SIMD register containing packed characters.
 * @return bool True if at least one byte is alphabetical, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
has_alphabet512(__m512i value) NOEXCEPT {
    return get_alphabet_flags512(value) != 0;
}

/**
 * @brief Checks if any character in a 512-bit vector is a numeric digit.
 * @details Evaluates if the 64-bit AVX-512 mask contains any set bits.
 * @param value The 512-bit SIMD register containing packed characters.
 * @return bool True if at least one byte is numeric, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
has_number512(__m512i value) NOEXCEPT {
    return get_number_flags512(value) != 0;
}

/**
 * @brief Checks if any character in a 512-bit vector is a whitespace character.
 * @details Evaluates if the 64-bit AVX-512 mask contains any set bits.
 * @param value The 512-bit SIMD register containing packed characters.
 * @return bool True if at least one byte is whitespace, false otherwise.
 */
[[nodiscard]] FORCE_INLINE bool
has_whitespace512(__m512i value) NOEXCEPT {
    return get_whitespace_flags512(value) != 0;
}

#endif // __AVX512BW__
#endif // SIMD

} // namespace zuu