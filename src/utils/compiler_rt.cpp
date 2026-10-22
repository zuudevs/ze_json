// ============================================================================
//  compiler_rt.cpp — stub builtin libcall untuk target WASM
// ============================================================================
//  Build wasm memakai -nostdlib, sehingga compiler-rt tidak ikut terlink.
//  Modul Lemire (number_parser.hpp) memakai unsigned __int128; tabrakan
//  instruksi multiply 128-bit tidak ada di WASM, sehingga clang memanggil
//  libcall __multi3 yang harus disediakan sendiri.
// ============================================================================

#include <ze_json/core/macros.hpp>
#include <ze_json/types/integral.hpp>

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)

extern "C" unsigned __int128
__multi3(unsigned __int128 a, unsigned __int128 b) NOEXCEPT {
    const zuu::types::uint64_t a_lo = static_cast<zuu::types::uint64_t>(a);
    const zuu::types::uint64_t a_hi =
        static_cast<zuu::types::uint64_t>(a >> 64);
    const zuu::types::uint64_t b_lo = static_cast<zuu::types::uint64_t>(b);
    const zuu::types::uint64_t b_hi =
        static_cast<zuu::types::uint64_t>(b >> 64);

    // Hasil low-128-bit dari a * b mod 2^128.
    // 64x64->128 widening multiply diturunkan compiler secara inline
    // (high-halam operand nol diketahui), sehingga tidak berujung ke
    // pemanggilan __multi3 rekursif.
    unsigned __int128 lo    = static_cast<unsigned __int128>(a_lo) * b_lo;
    unsigned __int128 cross = (static_cast<unsigned __int128>(a_hi) * b_lo) +
                              (static_cast<unsigned __int128>(a_lo) * b_hi);

    return lo + (cross << 64);
}

#endif // wasm target
