// JSON number parsing (TASKS P7-2): RFC 8259 grammar + correctly-rounded
// decimal -> binary conversion with V8-parity semantics.
//
// Paths (exactly ONE rounding everywhere — no double rounding):
//   1. all-zero digits        -> ±0.0 (sign preserved; V8 parses "-0" as -0)
//   2. pure integer, <= 19 digits -> single uint64 -> double conversion
//      (IEEE 754 conversion is correctly rounded)
//   3. mantissa < 2^53 (<= 15 digits) with |adjusted exponent| <= 22:
//      exact 128-bit product (q >= 0) or quotient + remainder (q < 0),
//      rounded once through the 55-bit + sticky machine
//   4. everything else: fixed-capacity big-integer slow path — exact
//      decimal significand, power-of-five scaling, 54-bit-long division
//      capturing remainder (sticky), rounded once. Self-contained: no
//      libc, so the wasm64 build (-nostdlib) stays correct.
//
// Exponent clamps: magnitude >= 10^309 -> ±Infinity, magnitude < 10^-325
// -> ±0 (both safely outside the tie window 2^-1075 ± ULP/4, so the clamp
// never changes a finite/tie result; V8 gives Infinity/0 for these).
//
// @cite 10.1002_spe.2984 number parsing at a gigabyte per second — the
//       mantissa/exponent decomposition, the single-rounding requirement,
//       and the fast/slow path split follow the paper; the power-of-five
//       table fast path (kNormalizedPowerOfFive/kPowerOfFiveReciprocal) is
//       the planned P4-3 optimization on top of this baseline.
#include <ze_json/ondemand/number.hpp>

#include <ze_json/core/integral.hpp>
#include <ze_json/core/lookups.hpp>

namespace zuu::ondemand {

namespace {

using Big = unsigned __int128;

constexpr Big kMask64 = (Big{1} << 64) - 1;

// ---------------------------------------------------------------- bit utils

[[nodiscard]] inline int bitlen64(core::uint64_t v) noexcept {
  return v == 0 ? 0 : 64 - __builtin_clzll(v);
}

[[nodiscard]] inline int bitlen128(Big v) noexcept {
  const core::uint64_t hi = static_cast<core::uint64_t>(v >> 64);
  if (hi != 0) {
    return 128 - __builtin_clzll(hi);
  }
  return bitlen64(static_cast<core::uint64_t>(v));
}

// 10^0..10^22 as 128-bit (10^20..10^22 exceed uint64).
constexpr Big kPow10Big(int p) noexcept {
  Big v = 1;
  for (int i = 0; i < p; ++i) {
    v *= 10;
  }
  return v;
}

// ------------------------------------------------------------- assembling

/**
 * Single rounding of `value = sig55 × 2^(e - 54)` (sig55 ∈ [2^54, 2^55))
 * to nearest, ties-to-even; subnormal- and overflow-safe.
 * `sticky` = nonzero tail beyond the 55 captured bits.
 */
[[nodiscard]] double assemble_double(core::uint64_t sig55, int e, bool sticky,
                                     bool negative) noexcept {
  // Normalized value = 1.f × 2^e (sig55 = 1.f × 2^54).
  if (e > 1023) {
    // V8 semantics: overflow -> ±Infinity.
    const core::uint64_t inf = core::uint64_t{0x7FF} << 52;
    double out;
    __builtin_memcpy(&out, &inf, sizeof(out));
    return negative ? -out : out;
  }
  if (e >= -1022) {
    // Normal: 53-bit significand = sig55 >> 2, rounding bit = bit 1.
    core::uint64_t sig = sig55 >> 2;
    const bool round_bit = ((sig55 >> 1) & 1) != 0;
    const bool tail = sticky || ((sig55 & 1) != 0);
    if (round_bit && (tail || (sig & 1) != 0)) {
      sig += 1;
      if (sig == (core::uint64_t{1} << 53)) {
        sig >>= 1; // 2^53 -> renormalize
        e += 1;
        if (e > 1023) {
          const core::uint64_t inf = core::uint64_t{0x7FF} << 52;
          double out;
          __builtin_memcpy(&out, &inf, sizeof(out));
          return negative ? -out : out;
        }
      }
    }
    const core::uint64_t bits = (static_cast<core::uint64_t>(e + 1023) << 52) |
                                (sig & ((core::uint64_t{1} << 52) - 1));
    double out;
    __builtin_memcpy(&out, &bits, sizeof(out));
    return negative ? -out : out;
  }
  // Subnormal (e <= -1023): round `value × 2^1074` = sig55 × 2^(e+1020) to
  // a u52 integer. sh = -(e+1020) >= 3 (e = -1022 is already normal).
  const int sh = -e - 1020;
  if (sh >= 64) {
    // Value below even the tie window: rounds to (signed) zero.
    const core::uint64_t zero = 0;
    double out;
    __builtin_memcpy(&out, &zero, sizeof(out));
    return negative ? -out : out;
  }
  core::uint64_t n = sig55 >> sh; // < 2^52 for sh >= 3
  bool round_bit = false;
  bool beyond = false; // bits strictly below the round-bit position
  if (sh <= 55) {
    round_bit = ((sig55 >> (sh - 1)) & 1) != 0;
    beyond = sh >= 2 && (sig55 & ((core::uint64_t{1} << (sh - 1)) - 1)) != 0;
  } else {
    beyond = sig55 != 0; // every captured bit lies below the (absent) round bit
  }
  if (round_bit && (sticky || beyond || (n & 1) != 0)) {
    n += 1;
  }
  core::uint64_t bits;
  if (n >= (core::uint64_t{1} << 52)) {
    // Rounded up into the minimum normal (1.0 × 2^-1022).
    bits = core::uint64_t{1} << 52;
  } else {
    bits = n; // subnormal: exponent field 0, mantissa = n
  }
  double out;
  __builtin_memcpy(&out, &bits, sizeof(out));
  return negative ? -out : out;
}

// ------------------------------------------------- exact 128-bit fast path

// Requires: 1 <= m < 2^53, -22 <= q <= 22.
[[nodiscard]] double region_exact(core::uint64_t m, core::int64_t q,
                                  bool negative) noexcept {
  core::uint64_t sig55 = 0;
  int e = 0;
  bool sticky = false;
  if (q >= 0) {
    // value = m × 10^q is an EXACT 128-bit integer (m < 2^53, 10^22 < 2^74).
    const Big t = static_cast<Big>(m) * kPow10Big(static_cast<int>(q));
    const int bl = bitlen128(t);
    if (bl > 55) {
      const int sh = bl - 55;
      sig55 = static_cast<core::uint64_t>(t >> sh);
      sticky = (t & ((Big{1} << sh) - 1)) != 0;
    } else {
      sig55 = static_cast<core::uint64_t>(t << (55 - bl));
    }
    e = bl - 1;
  } else {
    // value = m / 10^k, k in [1, 22]: divide by 5^k via the codegen
    // 128-bit reciprocal (kPowerOfFiveReciprocal, paper Section 9.1
    // scheme) instead of the bit-serial long division the reference
    // (region_div_ref) performs. Construction (k = -q, 1 <= k <= 22):
    //   z = bitlen(5^k), b = 127 + z, c = ceil(2^b / 5^k)  [table entry]
    //   l = 53 - bitlen(m); m_n = m << l (in [2^52, 2^53))
    //   n = m_n << z (in [2^(52+z), 2^(53+z)) < 2^106)
    //   N2 = (n × c) >> 128, D = 127 + z, Q = N2 >> (z - 1)
    //     = floor(n×c / 2^D) = floor(n / 5^k + eps), 0 <= eps < 2^-22,
    //     so Q in {floor(n/5^k), +1} — resolved by one exact compare.
    //   r = n - Q·5^k exact (u128); e = bl(Q) - 1 - (z + l + k).
    // Q in (2^52, 2^54) by the n/5^k slotting, so bl(Q) in {53, 54} and
    // the fold machinery below is unchanged.
    //
    // @cite 10.1002_spe.2984 Section 9.1 — division by powers of five via
    //       tabulated 128-bit reciprocals (single multiply product).
    const int k = static_cast<int>(-q);
    const Big d = kPow10Big(k) >> k;                 // = 5^k
    const int z = bitlen128(kPow10Big(k)) - k;       // = bitlen(5^k)
    const int l = 53 - bitlen64(m);
    const Big n = (static_cast<Big>(m) << l) << z;    // < 2^106
    const core::Bytes16 rec_entry = core::kPowerOfFiveReciprocal[k - 1];
    const Big c = (static_cast<Big>(rec_entry.high) << 64) | rec_entry.low;
    const Big n_hi = n >> 64, n_lo = n & kMask64;
    const Big c_hi = c >> 64, c_lo = c & kMask64;
    // N2 = (n × c) >> 128, assembled without the low half (bits below 128
    // cannot influence the Q bits — D = 127 + z >= 130).
    const Big t1 = n_hi * c_hi;                      // < 2^106
    const Big mid_a = n_hi * c_lo;
    const Big mid = mid_a + n_lo * c_hi;             // < 2^129
    const bool mid_carry = mid < mid_a;              // 128-bit wrap spill
    const Big mh = (mid >> 64) + (mid_carry ? 1 : 0);
    const Big tl = (mid & kMask64) << 64;            // mid's low × 2^64
    const Big tl2 = tl + n_lo * c_lo;                // < 2^129
    const bool tl_carry = tl2 < tl;                  // spill into bit 128
    const Big n2 = t1 + mh + (tl_carry ? 1 : 0);     // (n×c) >> 128
    Big qq = n2 >> (z - 1);                          // floor(n×c / 2^D)
    // Exact correction: floor(n/5^k) ∈ {qq, qq+1} (eps < 2^-22 can push
    // the truncated product one past the true quotient only).
    if (((qq + 1) * d) <= n) {
      qq += 1;
    }
    const Big rr = n - qq * d;                       // exact remainder
    const int s = z + l + k;                         // value = qq×2^-s scale
    // Fold the remainder into the 55-bit window — identical glue to the
    // reference (Semantic: dropping rr loses the rounding decision, e.g.
    // 0.1 must round UP; the quotient is < 4 by construction so the fold
    // uses comparisons only, never __udivti3).
    const int bl = bitlen128(qq);
    const int fold = 55 - bl; // bl ∈ {53, 54} -> fold ∈ {1, 2}
    if (fold < 0) {
      const int sh = -fold;
      sig55 = static_cast<core::uint64_t>(qq >> sh);
      sticky = sticky || (qq & ((Big{1} << sh) - 1)) != 0;
    } else {
      const Big shifted = rr << fold; // rr < d <= 2^52, fold <= 2 -> < 3x2^52
      Big add = 0;
      if (shifted >= d) {
        const Big d2 = d << 1;
        add = shifted >= d2 ? (shifted >= d2 + d ? Big{3} : Big{2}) : Big{1};
      }
      const Big rem2 = shifted - add * d;
      sig55 = static_cast<core::uint64_t>((qq << fold) + add);
      sticky = sticky || rem2 != 0;
    }
    e = bl - 1 - s;
  }
  return assemble_double(sig55, e, sticky, negative);
}

/**
 * @brief Bit-serial reference for region_exact's negative path (P4-3 C2).
 *
 * Same semantics as region_exact for q < 0 (requires 1 <= m < 2^53,
 * -22 <= q <= -1), but performs the division as binary long division
 * (≈ 53 + 3.3k shift/compare iterations) — the P7-2 proven implementation
 * that the reciprocal fast path must match double-for-double.
 *
 * @cite 10.1002_spe.2984 the exact-division anchor; kept as the
 *       correctness oracle (cpp-highperf-json rule: reference next to
 *       every fast path).
 */
[[nodiscard]] double region_div_ref(core::uint64_t m, core::int64_t q,
                                    bool negative) noexcept {
  core::uint64_t sig55 = 0;
  int e = 0;
  bool sticky = false;
  // value = m / 10^k as an exact rational; scale so the quotient lands in
  // (2^52, 2^54), divide bit-serially (≈ 53 + k iterations), round once.
  const int k = static_cast<int>(-q);
  const Big d = kPow10Big(k);
  const core::uint64_t n64 = m;
  const int s = 53 + bitlen128(d) - bitlen64(n64); // n << s < 2^127
  // Binary long division of (n << s) / d; bit i of (n << s) is bit (i-s)
  // of n (zero for i < s).
  Big qq = 0;
  Big rr = 0;
  const int top = bitlen64(n64) + s - 1;
  for (int i = top; i >= 0; --i) {
    rr = (rr << 1) |
         ((i >= s) ? ((static_cast<Big>(n64 >> (i - s))) & 1) : Big{0});
    if (rr >= d) {
      rr -= d;
      qq |= Big{1} << i;
    }
  }
  // value = (qq + rr/d) × 2^-s; qq ∈ (2^52, 2^54). Fold the remainder
  // into the 55-bit window — dropping it loses the rounding decision
  // (e.g. 0.1 must round UP from the quotient's truncation). The quotient
  // here is < 4 by construction, so it is computed with comparisons only
  // (no __udivti3 — the wasm64 build is -nostdlib).
  const int bl = bitlen128(qq);
  const int fold = 55 - bl; // bl ∈ {53, 54} -> fold ∈ {1, 2}
  if (fold < 0) {
    const int sh = -fold;
    sig55 = static_cast<core::uint64_t>(qq >> sh);
    sticky = sticky || (qq & ((Big{1} << sh) - 1)) != 0;
  } else {
    const Big shifted = rr << fold; // rr < d <= 2^74, fold <= 2 -> < 3x2^74
    Big add = 0;
    if (shifted >= d) {
      const Big d2 = d << 1;
      add = shifted >= d2 ? (shifted >= d2 + d ? Big{3} : Big{2}) : Big{1};
    }
    const Big rem2 = shifted - add * d;
    sig55 = static_cast<core::uint64_t>((qq << fold) + add);
    sticky = sticky || rem2 != 0;
  }
  e = bl - 1 - s;
  return assemble_double(sig55, e, sticky, negative);
}

// ------------------------------------------------------- big-integer slow path

// Fixed-capacity little-endian base-2^32 integer (no allocation: wasm64
// builds are -nostdlib). Capacity bounds: <= 768 significant digits
// (2552 bits) × 5^310 (+723 bits) = 3275 bits = 103 limbs.
constexpr core::size_t kBigLimbs = 112;

struct BigDec {
  core::uint32_t limb[kBigLimbs] = {0};
  core::size_t n = 0; // used limbs (0 = value 0)

  void zero_out() noexcept { n = 0; }

  /** Value-based zero test (n can exceed the value after shl1_or). */
  [[nodiscard]] bool is_zero() const noexcept {
    for (core::size_t i = 0; i < n; ++i) {
      if (limb[i] != 0) {
        return false;
      }
    }
    return true;
  }

  /** value = value << bits (bits >= 0; the caller bounds the capacity). */
  void shl(int bits) noexcept {
    if (bits <= 0 || is_zero()) {
      return;
    }
    const core::size_t limb_shift = static_cast<core::size_t>(bits) / 32;
    const int bit_shift = bits % 32;
    const core::size_t new_n = (static_cast<core::size_t>(bitlen()) +
                                static_cast<core::size_t>(bits) + 31) /
                               32;
    // Walk destinations top-down: every source index is <= its destination,
    // so in-place widening reads never race the writes.
    for (core::size_t dst = new_n; dst-- > 0;) {
      const long long src =
          static_cast<long long>(dst) - static_cast<long long>(limb_shift);
      core::uint64_t hi = 0;
      core::uint64_t lo = 0;
      if (src >= 0 && src < static_cast<long long>(n)) {
        hi = limb[src];
      }
      if (bit_shift != 0 && src >= 1 && src - 1 < static_cast<long long>(n)) {
        lo = limb[src - 1];
      }
      const core::uint64_t combined =
          bit_shift != 0 ? ((hi << bit_shift) | (lo >> (32 - bit_shift))) : hi;
      limb[dst] = static_cast<core::uint32_t>(combined);
    }
    n = new_n;
    while (n > 0 && limb[n - 1] == 0) {
      --n;
    }
  }

  /** value = value × m + add; m < 2^31, add < m. */
  void mul_add(core::uint32_t m, core::uint32_t add) noexcept {
    core::uint64_t carry = add;
    for (core::size_t i = 0; i < n; ++i) {
      const core::uint64_t cur =
          static_cast<core::uint64_t>(limb[i]) * m + carry;
      limb[i] = static_cast<core::uint32_t>(cur);
      carry = cur >> 32;
    }
    while (carry != 0 && n < kBigLimbs) {
      limb[n++] = static_cast<core::uint32_t>(carry);
      carry >>= 32;
    }
  }

  [[nodiscard]] int bitlen() const noexcept {
    if (n == 0) {
      return 0;
    }
    return static_cast<int>((n - 1) * 32) + bitlen64(limb[n - 1]);
  }

  [[nodiscard]] bool get_bit(int i) const noexcept {
    if (i < 0) {
      return false;
    }
    const core::size_t li = static_cast<core::size_t>(i) / 32;
    return li < n && ((limb[li] >> (i % 32)) & 1) != 0;
  }

  /** r = r × 2 + bit. */
  void shl1_or(bool bit) noexcept {
    core::uint32_t carry = bit ? 1u : 0u;
    const core::size_t need = (bitlen() + 2 + 31) / 32;
    if (need > n && need <= kBigLimbs) {
      n = need;
    }
    for (core::size_t i = 0; i < n; ++i) {
      const core::uint32_t next_carry = limb[i] >> 31;
      limb[i] = (limb[i] << 1) | carry;
      carry = next_carry;
    }
    if (carry != 0 && n < kBigLimbs) {
      limb[n++] = carry;
    }
  }

  /** Returns true when *this >= other (value comparison; zero-padding safe). */
  [[nodiscard]] bool ge(const BigDec &other) const noexcept {
    core::size_t a = n;
    while (a > 0 && limb[a - 1] == 0) {
      --a;
    }
    core::size_t b = other.n;
    while (b > 0 && other.limb[b - 1] == 0) {
      --b;
    }
    if (a != b) {
      return a > b;
    }
    for (core::size_t i = a; i > 0; --i) {
      if (limb[i - 1] != other.limb[i - 1]) {
        return limb[i - 1] > other.limb[i - 1];
      }
    }
    return true; // equal
  }

  void sub(const BigDec &other) noexcept { // *this >= other guaranteed
    core::uint64_t borrow = 0;
    for (core::size_t i = 0; i < n; ++i) {
      const core::uint64_t cur =
          static_cast<core::uint64_t>(limb[i]) - other.limb[i] - borrow;
      limb[i] = static_cast<core::uint32_t>(cur);
      borrow = (cur >> 32) != 0 ? 1 : 0; // 0xFFFFFFFF... means borrowed
    }
    while (n > 0 && limb[n - 1] == 0) {
      --n;
    }
  }
};

// value = 5^k (k >= 0, clamped callers keep k <= ~350).
[[nodiscard]] BigDec pow5(int k) noexcept {
  BigDec v;
  v.limb[0] = 1;
  v.n = 1;
  for (int i = 0; i < k; ++i) {
    v.mul_add(5, 0);
  }
  return v;
}

/** value = 5^q × A (q >= 0). */
[[nodiscard]] BigDec mul_pow5(BigDec a, int q) noexcept {
  for (int i = 0; i < q; ++i) {
    a.mul_add(5, 0);
  }
  return a;
}

// Requires sig_len >= 1; value = digits(as integer) exactly.
[[nodiscard]] BigDec big_from_digits(const char *digits,
                                     core::size_t len) noexcept {
  BigDec v;
  core::size_t i = 0;
  while (i < len) {
    core::uint32_t chunk = 0;
    core::uint32_t scale = 1;
    for (int j = 0; j < 9 && i < len; ++j, ++i) {
      chunk = chunk * 10 + static_cast<core::uint32_t>(digits[i] - '0');
      scale *= 10;
    }
    v.mul_add(scale, chunk); // value = value × scale + chunk
  }
  return v;
}

// Slow path: correctly-rounded value of `0.digits × 10^q0` semantics —
// `digits` holds the significant-digit string (no leading zeros; trailing
// zeros already folded into q0 by the caller), capped by the caller.
[[nodiscard]] double slow_path(const char *digits, core::size_t sig_len,
                               core::int64_t q0, bool negative,
                               bool tail_nonzero) noexcept {
  // Order-of-magnitude clamps (safe: strictly outside the tie window).
  if (q0 + static_cast<core::int64_t>(sig_len) - 1 >= 309) {
    const core::uint64_t inf = core::uint64_t{0x7FF} << 52;
    double out;
    __builtin_memcpy(&out, &inf, sizeof(out));
    return negative ? -out : out;
  }
  if (q0 + static_cast<core::int64_t>(sig_len) <= -325) {
    const core::uint64_t z = 0;
    double out;
    __builtin_memcpy(&out, &z, sizeof(out));
    return negative ? -out : out;
  }

  bool sticky = tail_nonzero;
  const BigDec a = big_from_digits(digits, sig_len);

  if (q0 >= 0) {
    // value = A × 5^q0 × 2^q0, exact.
    const BigDec m = mul_pow5(a, static_cast<int>(q0));
    const int bl = m.bitlen();
    // sig55: bit b of sig55 is bit (bl-55+b) of m (negative index = 0,
    // which is exactly the left-shift normalization when bl < 55).
    core::uint64_t sig55 = 0;
    for (int b = 0; b < 55; ++b) {
      if (m.get_bit(bl - 55 + b)) {
        sig55 |= core::uint64_t{1} << b;
      }
    }
    // Sticky: any bit of m strictly below position (bl-55).
    bool st = sticky;
    const int low = bl - 55;
    if (low > 0) {
      const core::size_t full_limbs = static_cast<core::size_t>(low) / 32;
      for (core::size_t li = 0; li < full_limbs && !st; ++li) {
        st = m.limb[li] != 0;
      }
      const core::size_t partial = static_cast<core::size_t>(low) % 32;
      if (!st && partial != 0 && full_limbs < m.n) {
        st = (m.limb[full_limbs] & ((core::uint32_t{1} << partial) - 1)) != 0;
      }
    }
    const auto e = static_cast<int>(bl - 1 + q0);
    return assemble_double(sig55, e, st, negative);
  }

  // q0 < 0: value = A × 2^q0 / 5^k, k = -q0. Long-divide A×2^s2 by B with
  // s2 chosen so the quotient lands in [2^55, 2^57) (55 captured bits +
  // rounding) — the two EXTRA numerator bits fold the remainder into the
  // 55-bit window exactly like the fast path does (dropping them loses
  // round-up cases). s2 can go NEGATIVE (huge mantissa, small k): then the
  // DIVISOR is shifted instead (divisor bits = ba - 56, always in capacity).
  const int k = static_cast<int>(-q0);
  const BigDec b = pow5(k);
  const int ba = a.bitlen();
  const int bb = b.bitlen();
  const auto s2 = static_cast<core::int64_t>(56) + bb - ba;
  const auto total_bits = static_cast<core::int64_t>(ba) + (s2 > 0 ? s2 : 0);

  BigDec r;    // remainder < divisor
  BigDec qq;   // quotient (bitlen 56-58)
  BigDec dive; // divisor (B, possibly B << -s2)
  if (s2 >= 0) {
    dive = b;
  } else {
    dive = b;
    dive.shl(static_cast<int>(-s2));
  }
  for (auto i = total_bits - 1; i >= 0; --i) {
    const bool bit = (s2 >= 0 && i >= s2)
                         ? a.get_bit(static_cast<int>(i - s2))
                         : (s2 < 0 ? a.get_bit(static_cast<int>(i)) : false);
    r.shl1_or(bit);
    const bool take = r.ge(dive);
    if (take) {
      r.sub(dive);
      qq.shl1_or(true);
    } else {
      qq.shl1_or(false);
    }
  }
  sticky = sticky || !r.is_zero();

  const int bl = qq.bitlen(); // 56-58 by construction
  const int sh = bl - 55;
  core::uint64_t sig55 = 0;
  for (int bidx = 0; bidx < 55; ++bidx) {
    if (qq.get_bit(sh + bidx)) {
      sig55 |= core::uint64_t{1} << bidx;
    }
  }
  // Bits of the quotient dropped by the normalization are pure sticky.
  for (int db = 0; db < sh && !sticky; ++db) {
    if (qq.get_bit(db)) {
      sticky = true;
    }
  }
  const auto e = static_cast<int>(static_cast<core::int64_t>(bl) - 1 + q0 - s2);
  return assemble_double(sig55, e, sticky, negative);
}

} // namespace

// ------------------------------------------------------------------ scanning

core::Result<NumberParts, core::JsonErrc>
scan_number(const char *src, core::size_t len) NOEXCEPT {
  if (len == 0) {
    return core::JsonErrc::InvalidJson;
  }
  core::size_t i = 0;
  bool negative = false;
  if (src[0] == '-') {
    negative = true;
    i = 1;
    if (len == 1) {
      return core::JsonErrc::InvalidJson;
    }
  }

  const core::size_t digits_start = i;
  core::size_t int_len = 0;
  if (src[i] == '0') {
    int_len = 1;
    i += 1;
    if (i < len && src[i] >= '0' && src[i] <= '9') {
      return core::JsonErrc::InvalidJson; // leading zero ("01")
    }
  } else if (src[i] >= '1' && src[i] <= '9') {
    while (i < len && src[i] >= '0' && src[i] <= '9') {
      i += 1;
    }
    int_len = i - digits_start;
  } else {
    return core::JsonErrc::InvalidJson; // '.', '+', letter, ...
  }

  core::size_t frac_len = 0;
  if (i < len && src[i] == '.') {
    i += 1;
    if (i >= len || src[i] < '0' || src[i] > '9') {
      return core::JsonErrc::InvalidJson; // "1." / "1.e5"
    }
    while (i < len && src[i] >= '0' && src[i] <= '9') {
      i += 1;
    }
    frac_len = i - digits_start - int_len - 1;
  }
  const core::size_t digits_len = int_len + frac_len; // dot excluded

  bool has_exp = false;
  bool exp_negative = false;
  core::uint64_t exp_value = 0;
  if (i < len && (src[i] == 'e' || src[i] == 'E')) {
    has_exp = true;
    i += 1;
    if (i < len && (src[i] == '+' || src[i] == '-')) {
      exp_negative = src[i] == '-';
      i += 1;
    }
    if (i >= len || src[i] < '0' || src[i] > '9') {
      return core::JsonErrc::InvalidJson; // "1e" / "1e+"
    }
    while (i < len && src[i] >= '0' && src[i] <= '9') {
      exp_value = exp_value * 10 + static_cast<core::uint64_t>(src[i] - '0');
      if (exp_value > 1000000) {
        exp_value = 1000000; // clamp: >= 400 decides Inf/0 outright
      }
      i += 1;
    }
  }

  // Zero detection: every DIGIT byte is '0' (the dot is not a digit).
  bool zero = true;
  for (core::size_t k = 0; k < int_len; ++k) {
    if (src[digits_start + k] != '0') {
      zero = false;
      break;
    }
  }
  for (core::size_t k = 0; zero && k < frac_len; ++k) {
    if (src[digits_start + int_len + 1 + k] != '0') {
      zero = false;
    }
  }

  return NumberParts{negative, zero,    digits_start, digits_len, int_len,
                     frac_len, has_exp, exp_negative, exp_value,  i};
}

// ------------------------------------------------------------------- parsing

namespace {

[[nodiscard]] double make_zero(bool negative) noexcept {
  const core::uint64_t z = 0;
  double out;
  __builtin_memcpy(&out, &z, sizeof(out));
  return negative ? -out : out;
}

} // namespace

core::Result<double, core::JsonErrc> parse_number(const char *src,
                                                  core::size_t len) NOEXCEPT {
  const core::Result<NumberParts, core::JsonErrc> scanned =
      scan_number(src, len);
  if (scanned.has_error()) {
    return scanned.error();
  }
  const NumberParts &p = scanned.value();
  if (p.zero) {
    return make_zero(p.negative);
  }

  // Logical digit string: integer-part digits followed by fractional-part
  // digits, with the '.' skipped.
  const char *const d = src + p.digits_start;
  const bool has_dot = p.frac_len != 0;
  auto dig = [&](core::size_t k) -> char {
    return (has_dot && k >= p.int_len) ? d[k + 1] : d[k];
  };

  // Significant window: strip leading and trailing zeros (trailing zeros
  // fold into the exponent — they are exact decimal rescalings).
  core::size_t first = 0;
  while (dig(first) == '0') {
    first += 1;
  }
  core::size_t last = p.digits_len - 1;
  while (dig(last) == '0') {
    last -= 1;
  }
  const core::size_t sig_len = last - first + 1;

  const core::int64_t exp_term =
      p.has_exp ? (p.exp_negative ? -static_cast<core::int64_t>(p.exp_value)
                                  : static_cast<core::int64_t>(p.exp_value))
                : 0;
  const core::int64_t q0 = exp_term - static_cast<core::int64_t>(p.frac_len) +
                           static_cast<core::int64_t>(p.digits_len - 1 - last);

  // Fold the leading significant digits (enough for every fast path).
  core::uint64_t m = 0;
  const core::size_t fold = sig_len < 19 ? sig_len : 19;
  for (core::size_t k = 0; k < fold; ++k) {
    m = m * 10 + static_cast<core::uint64_t>(dig(first + k) - '0');
  }

  // Path 2: pure integers (no fraction, no exponent, no trailing zeros —
  // any of those makes q0 != 0) up to 19 digits: a single correctly-rounded
  // uint64 -> double conversion.
  if (q0 == 0 && sig_len <= 19) {
    const double v = static_cast<double>(m);
    return p.negative ? -v : v;
  }

  // Path 3: exact 128-bit region (m < 2^53 because sig_len <= 15).
  if (sig_len <= 15 && q0 >= -22 && q0 <= 22) {
    return region_exact(m, q0, p.negative);
  }

  // Path 4: big-integer slow path on the capped significant-digit buffer.
  // Capping drops the LEAST significant digits: they fold into the
  // exponent (value = A_capped × 10^(q0 + dropped)) and their nonzero
  // remainder becomes sticky. Without the exponent fold the value would
  // be wrong by 10^dropped.
  constexpr core::size_t kSigCap = 768;
  char sigbuf[kSigCap];
  bool tail_nonzero = false;
  core::int64_t dropped = 0;
  if (sig_len > kSigCap) {
    for (core::size_t k = 0; k < kSigCap; ++k) {
      sigbuf[k] = dig(first + k);
    }
    dropped = static_cast<core::int64_t>(sig_len - kSigCap);
    tail_nonzero = true; // the dropped window contains the nonzero `last`
  } else {
    for (core::size_t k = 0; k < sig_len; ++k) {
      sigbuf[k] = dig(first + k);
    }
  }
  return slow_path(sigbuf, sig_len < kSigCap ? sig_len : kSigCap, q0 + dropped,
                   p.negative, tail_nonzero);
}

// ----------------------------------------------------- verification bridge

// P4-3 C2 test bridge (see number.hpp `detail`): thin forwarders so
// tests/number_test.cpp can drive the two negative-region paths directly.
// The anonymous-namespace implementations stay private to this TU; the
// bridge functions merely translate the test's k to the internal q = -k.
double detail::region_exact_neg(const core::uint64_t m, const int k,
                                const bool negative) noexcept {
  return region_exact(m, -k, negative);
}

double detail::region_div_ref(const core::uint64_t m, const int k,
                              const bool negative) noexcept {
  return region_div_ref(m, -k, negative);
}

} // namespace zuu::ondemand
