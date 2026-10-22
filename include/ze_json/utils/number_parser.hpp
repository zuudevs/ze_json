#pragma once

#include <ze_json/lookups/powers_of_five.hpp>
#include <ze_json/lookups/powers_of_ten.hpp>
#include <ze_json/types/integral.hpp>

namespace zuu::utils {

constexpr double
parse_double(const char* p) NOEXCEPT {
    // Implementasi berdasarkan:
    // "Number parsing at a gigabyte per second" - Daniel Lemire (2021)

    // Toleransi whitespace di awal (contoh: `"key": 1048576`)
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }

    bool negative = false;
    if (*p == '-') {
        negative = true;
        p++;
    }

    // w = decimal significand
    // q = decimal exponent
    types::uint64_t w         = 0;
    types::int64_t q          = 0;
    types::int32_t sig_digits = 0;

    // TAHAP 1: SECTION 4 (PARSING THE STRING)

    // 1. Lewati leading zeros
    while (*p == '0') {
        p++;
    }

    // 2. Parsing angka bulat (Maksimal 19 digit untuk menghindari overflow
    // uint64_t)
    while (*p >= '0' && *p <= '9') {
        if (sig_digits < 19) {
            w = (w * 10) + static_cast<types::uint64_t>(*p - '0');
            sig_digits++;
        } else {
            q++; // Lemire: Digit ke-20+ diabaikan, tapi eksponen ditambah
        }
        p++;
    }

    // 3. Parsing angka desimal (Fractional)
    if (*p == '.') {
        p++;
        // Jika leading zeros di belakang koma (contoh: 0.000123)
        if (sig_digits == 0) {
            while (*p == '0') {
                q--;
                p++;
            }
        }
        // Lanjutkan mengambil digit signifikan
        while (*p >= '0' && *p <= '9') {
            if (sig_digits < 19) {
                w = (w * 10) + static_cast<types::uint64_t>(*p - '0');
                sig_digits++;
                q--;
            }
            p++;
        }
    }

    // 4. Parsing scientific notation (e/E)
    if (*p == 'e' || *p == 'E') {
        p++;
        bool exp_negative = false;
        if (*p == '-') {
            exp_negative = true;
            p++;
        } else if (*p == '+') {
            p++;
        }

        types::int64_t exp = 0;
        while (*p >= '0' && *p <= '9') {
            exp = (exp * 10) + static_cast<types::int64_t>(*p - '0');
            p++;
        }

        if (exp_negative) {
            q -= exp;
        } else {
            q += exp;
        }
    }

    // TAHAP 2: CLINGER'S FAST PATH (Section 3 dari paper Lemire)
    double result = 0.0;

    if (w == 0) {
        result = 0.0;
    }
    // 9007199254740992 adalah 2^53 (Batas akurasi eksak IEEE-754 binary64)
    else if (q >= -22 && q <= 22 && w <= 9007199254740992ULL) {
        if (q < 0) {
            result = static_cast<double>(w) / lookups::kPowersOfTen[-q];
        } else {
            result = static_cast<double>(w) * lookups::kPowersOfTen[q];
        }
    } else {
        // TAHAP 3: LEMIRE'S EXACT FALLBACK (Algorithm 1)
        // Mengeksekusi manual bit-banging untuk IEEE-754 binary64

        // 1. Pengecekan limit ekstrim (Underflow / Overflow)
        if (q < -342)
            return negative ? -0.0 : 0.0;
        if (q > 308)
            return negative ? -__builtin_inf() : __builtin_inf();

        // 2. Normalisasi significand (l = leading zeros)
        types::int32_t l       = __builtin_clzll(w);
        types::uint64_t w_norm = w << l;

        // 3. Ambil 128-bit konstan (T[q]) dari tabel
        lookups::FloatTable T = (q >= 0) ? lookups::kPowerOfFive128[q]
                                         : lookups::kPowerOfFive128Inverse[-q];

        // 4. Eksekusi 128-bit Truncated Multiplication: z = (T[q] * w) / 2^64
        // Memanfaatkan compiler intrinsics __int128 karena kita berjalan di
        // wasm64 / x64
        using u128         = unsigned __int128;
        u128 w128          = w_norm;
        u128 first_product = w128 * T.low;
        u128 z             = (w128 * T.high) + (first_product >> 64);

        types::uint64_t z_high = static_cast<types::uint64_t>(z >> 64);

        // 5. Hitung binary significand & lakukan pembulatan (Rounding)
        types::uint32_t u       = static_cast<types::uint32_t>(z_high >> 63);
        types::uint64_t m       = u ? (z_high >> 10) : (z_high >> 9);
        types::uint64_t tie_bit = u ? (z_high & 0x200) : (z_high & 0x100);

        if (tie_bit) {
            m++; // Round up
        }
        m >>= 1; // Trim ke 53 bits IEEE-754 mantissa standard

        // 6. Hitung binary exponent p (Formula efisien menggunakan shift dari
        // paper Lemire)
        types::int64_t p = ((217706LL * q) >> 16) + 63 - l + u;

        if (m >= (1ULL << 53)) {
            m >>= 1;
            p++;
        }

        // 7. Assemble (Rakit) nilai floating-point (Bit-Banging)
        types::int64_t exp = p + 1023; // 1023 adalah bias eksponen IEEE-754
        types::uint64_t double_bits = (negative ? (1ULL << 63) : 0);

        if (exp <= 0) {
            // Subnormal Value Handling
            types::int64_t shift = 1 - exp;
            if (shift < 54) {
                m >>= shift;
                double_bits |= (m & 0xFFFFFFFFFFFFFULL);
            }
        } else if (exp >= 2047) {
            // Infinity Handling
            double_bits |= (2047ULL << 52);
        } else {
            // Normal Number Handling
            double_bits |= (static_cast<types::uint64_t>(exp) << 52);
            double_bits |= (m & 0xFFFFFFFFFFFFFULL);
        }

        // Cast hasil bit murni ke dalam tipe data double secara paksa
        // (Zero-Cost)
        __builtin_memcpy(&result, &double_bits, sizeof(double));
    }

    // Jika melewati Clinger's fast path, kita harus mengaplikasikan minus
    // secara manual (Karena di jalur Lemire, minus sudah dimasukkan langsung ke
    // sign-bit IEEE-754)
    if (negative && q >= -22 && q <= 22 && w <= 9007199254740992ULL) {
        result = -result;
    }

    return result;
}

// Helper tambahan jika user yakin datanya hanya integer murni (lebih cepat)
static constexpr types::int64_t
parse_int(const char* p) NOEXCEPT {
    // Toleransi whitespace di awal (contoh: `"key": 1048576`)
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }

    bool negative = false;
    if (*p == '-') {
        negative = true;
        p++;
    }
    types::int64_t w = 0;
    while (*p >= '0' && *p <= '9') {
        w = (w * 10) + static_cast<types::int64_t>(*p - '0');
        p++;
    }
    return negative ? -w : w;
}

} // namespace zuu::utils