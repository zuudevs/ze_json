// String token scan + unescape differential + correctness tests (P7-3).
//
// Rationale: the string layer must agree with the comparison baseline
// (V8 JSON.parse, probed empirically on 2026-11-10 — see the V8ParityEdges
// truth table) before any performance claim is admissible (cpp-highperf-json
// rule 4; AGENTS.md section 6). Gates:
//   - RFC 8259 grammar cases + truncation prefixes (hard-coded verdicts);
//   - exhaustive \uXXXX sweep (65536) — every escape value legal, decoded
//     bytes = UTF-8 of the code point, surrogates pass through;
//   - surrogate pair matrix sweep (256 x 256 = 65536) — astral code points;
//   - lone-surrogate pass-through incl. hex case preservation;
//   - raw strict-UTF-8 validity (the P7-1 anchor semantics, re-verified);
//   - reference-differential fuzz (valid + corrupted tokens).
//
// @cite 10.48550_arXiv.1902.08318 simdjson — the linear string pass these
//       gates pin down (escape recognition + body materialization).
// @cite 10.14778_3115404.3115416 Mison — span/escape-count decomposition.
#include <gtest/gtest.h>

#include <ze_json/ondemand/string.hpp>

#include "export_sidecar.hpp" // export-helper sidecar contract (AGENTS.md section 4)

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using zuu::ondemand::StringParts;
using zuu::ondemand::scan_string;
using zuu::ondemand::unescape_string;

/** Expected strict-UTF-8 bytes of a code point (hand-rolled encoder). */
[[nodiscard]] std::string expected_utf8(std::uint32_t cp) {
  std::string out;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return out;
}

/** 4-digit hex `\uXXXX` text, upper/lower case controllable. */
[[nodiscard]] std::string u4(std::uint32_t cp, bool upper) {
  static const char kLower[] = "0123456789abcdef";
  static const char kUpper[] = "0123456789ABCDEF";
  const char *digits = upper ? kUpper : kLower;
  std::string out = "\\u";
  for (int shift = 12; shift >= 0; shift -= 4) {
    out.push_back(digits[(cp >> shift) & 0xF]);
  }
  return out;
}

[[nodiscard]] std::string token_of(const std::string &body) {
  return "\"" + body + "\"";
}

/** Acceptance probe via scan_string. */
[[nodiscard]] bool accepted(const std::string &token) noexcept {
  return scan_string(token.data(), token.size()).has_value();
}

/** Full decode; on success `out` = decoded bytes, returns ok. */
[[nodiscard]] bool decode_to(const std::string &token, std::string &out) {
  out.clear();
  const auto s = scan_string(token.data(), token.size());
  if (s.has_error()) {
    return false;
  }
  std::vector<char> buf(s.value().inner_len + 8);
  const auto d = unescape_string(token.data(), token.size(), buf.data(),
                                 buf.size());
  if (d.has_error()) {
    return false;
  }
  // Contract property: decoded output never exceeds the interior length.
  if (d.value() > s.value().inner_len) {
    ADD_FAILURE() << "decoded " << d.value() << " > inner "
                  << s.value().inner_len << " for " << token;
    return false;
  }
  out.assign(buf.data(), d.value());
  return true;
}

// ------------------------------------------------------------------ grammar

TEST(StringGrammar, ValidTokens) {
  const std::array<std::string, 10> tokens = {
    token_of(""),                                // empty
    token_of("abc DEF 42"),                      // plain
    token_of("\\\"\\\\"),                        // quote + backslash escapes
    token_of("\\b\\f\\n\\r\\t\\/"),              // all remaining simple escapes
    token_of(u4(0x41, true) + u4(0x1F, false)),  // hex, both cases
    token_of("\xF0\x9F\x98\x80\xE2\xAC\x80"),    // raw 4+3-byte
    token_of("\xC2\x80\x7F"),                    // raw 2-byte + raw DEL
    token_of("\\uD83D\\uDE00"),                  // surrogate pair
    token_of("\\u0001"),                         // escaped control value
    token_of("\xEF\xBF\xBD"),                    // raw U+FFFD
  };
  for (const std::string &tok : tokens) {
    EXPECT_TRUE(accepted(tok)) << tok;
  }
  // Escapes + raw DEL mixed:
  EXPECT_TRUE(accepted(token_of("\\\"x\\\\\\t\x7F")));
}

TEST(StringGrammar, InvalidTokens) {
  // No opening quote / empty buffer:
  EXPECT_TRUE(scan_string("abc", 3).has_error());
  EXPECT_TRUE(scan_string("", 0).has_error());
  EXPECT_TRUE(scan_string("\\", 1).has_error());
  const std::array<std::string, 8> bad = {
    token_of("\\x41"), token_of("\\v"),  token_of("\\'"),
    token_of("\\Q"),   token_of("\\0"),  token_of("\\u12"),
    token_of("\\uzzzz"), token_of("\\u00 1"),
  };
  for (const std::string &tok : bad) {
    EXPECT_TRUE(scan_string(tok.data(), tok.size()).has_error()) << tok;
  }
  // Trailing backslash (interior ends with a bare backslash):
  const std::string trailing = "\"x\\\\";
  EXPECT_TRUE(scan_string(trailing.data(), trailing.size()).has_error())
      << trailing;
  // Raw control byte inside the body:
  const std::string ctrl = "\"a\x01b\"";
  EXPECT_TRUE(scan_string(ctrl.data(), ctrl.size()).has_error()) << ctrl;
}

TEST(StringGrammar, RawControlSweep) {
  // Every raw byte 0x00..0x1F is illegal; every ASCII byte 0x20..0x7F
  // except '"' and '\' is legal (DEL 0x7F included).
  for (int c = 0; c <= 0x1F; ++c) {
    std::string tok;
    tok.push_back('"');
    tok.push_back(static_cast<char>(c));
    tok.push_back('"');
    EXPECT_TRUE(scan_string(tok.data(), tok.size()).has_error())
        << "raw byte 0x" << std::hex << c;
  }
  for (int c = 0x20; c <= 0x7F; ++c) {
    if (c == '"' || c == '\\') {
      continue;
    }
    std::string tok;
    tok.push_back('"');
    tok.push_back(static_cast<char>(c));
    tok.push_back('"');
    EXPECT_TRUE(accepted(tok)) << "raw byte 0x" << std::hex << c;
  }
}

TEST(StringGrammar, PrefixConsumption) {
  // The on-demand parser calls scan positioned on the opening quote; the
  // consumed span ends at the CLOSING quote — trailing bytes are the
  // CALLER's delimiter check, not the string layer's.
  const char *doc = "[\"ab\",1]x";
  const auto s = scan_string(doc + 1, std::strlen(doc) - 1);
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s.value().total_len, 4); // "ab" = 4 bytes incl. quotes
  EXPECT_EQ(s.value().inner_len, 2);
  EXPECT_EQ(s.value().escape_count, 0);
  // Escaped version: "a\tb" (body = a backslash t b — 4 SOURCE bytes).
  const char *doc2 = "[\"a\\tb\",]";
  const auto s2 = scan_string(doc2 + 1, std::strlen(doc2) - 1);
  ASSERT_TRUE(s2.has_value());
  EXPECT_EQ(s2.value().total_len, 6);
  EXPECT_EQ(s2.value().inner_len, 4);
  EXPECT_EQ(s2.value().escape_count, 1);
}

// ------------------------------------------------------------------- V8

TEST(StringSemantics, V8ParityEdges) {
  // Truth table from the V8 JSON.parse probe (2026-11-10).
  std::string out;

  EXPECT_TRUE(decode_to(token_of("\\ud800"), out));
  EXPECT_EQ(out, "\\ud800"); // lone high passes through verbatim
  EXPECT_TRUE(decode_to(token_of("\\udc00"), out));
  EXPECT_EQ(out, "\\udc00"); // lone low passes through verbatim
  EXPECT_TRUE(decode_to(token_of("\\uD800"), out));
  EXPECT_EQ(out, "\\uD800"); // hex case preserved
  EXPECT_TRUE(decode_to(token_of("\\ud800B"), out));
  EXPECT_EQ(out, "\\ud800B"); // hi then a non-surrogate escape text
  EXPECT_TRUE(decode_to(token_of("\\ud800\\ud800"), out));
  EXPECT_EQ(out, "\\ud800\\ud800"); // two lone highs: never paired
  EXPECT_TRUE(decode_to(token_of("\\udc00\\ud800"), out));
  EXPECT_EQ(out, "\\udc00\\ud800"); // a low cannot pair backward

  EXPECT_TRUE(decode_to(token_of("\\u0000"), out));
  EXPECT_EQ(out, std::string(1, '\0')); // escaped NUL -> 0x00 byte

  EXPECT_TRUE(decode_to(token_of("\\ud83d\\ude00"), out));
  EXPECT_EQ(out, expected_utf8(0x1F600)); // pair -> astral 4-byte UTF-8
  EXPECT_TRUE(decode_to(token_of("\\ud83D\\uDE00"), out));
  EXPECT_EQ(out, expected_utf8(0x1F600)); // pair, mixed hex case
  EXPECT_TRUE(decode_to(token_of("\\uDBFF\\uDFFF"), out));
  EXPECT_EQ(out, expected_utf8(0x10FFFF)); // maximal pair

  EXPECT_TRUE(decode_to(token_of("\\u007F"), out));
  EXPECT_EQ(out, std::string(1, '\x7F')); // escaped DEL
  EXPECT_TRUE(decode_to(token_of("\\u001F"), out));
  EXPECT_EQ(out, std::string(1, '\x1F')); // escaped control byte's value
  EXPECT_TRUE(decode_to(token_of("\\uFFFF"), out));
  EXPECT_EQ(out, expected_utf8(0xFFFF)); // noncharacter still a value
}

TEST(StringSemantics, SimpleEscapes) {
  const std::array<std::pair<const char *, char>, 8> table{{
      {"\\\"", '"'}, {"\\\\", '\\'}, {"\\/", '/'}, {"\\b", '\b'},
      {"\\f", '\f'}, {"\\n", '\n'}, {"\\r", '\r'}, {"\\t", '\t'},
  }};
  for (const auto &[esc, byte] : table) {
    std::string out;
    EXPECT_TRUE(decode_to(token_of(esc), out)) << esc;
    EXPECT_EQ(out, std::string(1, byte)) << esc;
  }
  // "empty string" empty-decode:
  std::string none;
  EXPECT_TRUE(decode_to(token_of(""), none));
  EXPECT_TRUE(none.empty());
}

TEST(StringSemantics, ExhaustiveUSweep) {
  // EVERY \uXXXX value 0x0000..0xFFFF is a legal token; the decode is the
  // UTF-8 of the code point for non-surrogates and a verbatim pass-through
  // for surrogates. Both hex cases behave identically.
  std::string out;
  for (std::uint32_t cp = 0; cp <= 0xFFFF; ++cp) {
    const bool surro = cp >= 0xD800 && cp <= 0xDFFF;
    const std::string lo_tok = token_of(u4(cp, false));
    ASSERT_TRUE(accepted(lo_tok)) << lo_tok;
    ASSERT_TRUE(decode_to(lo_tok, out)) << lo_tok;
    ASSERT_EQ(out, surro ? u4(cp, false) : expected_utf8(cp)) << lo_tok;
    const std::string up_tok = token_of(u4(cp, true));
    ASSERT_TRUE(accepted(up_tok)) << up_tok;
    ASSERT_TRUE(decode_to(up_tok, out)) << up_tok;
    ASSERT_EQ(out, surro ? u4(cp, true) : expected_utf8(cp)) << up_tok;
  }
}

TEST(StringSemantics, PairMatrixSweep) {
  // Every high x low surrogate pair decodes to the astral code point as
  // 4-byte UTF-8; token interior is exactly 12 bytes for a full pair.
  std::string out;
  for (std::uint32_t hi = 0xD800; hi <= 0xDBFF; ++hi) {
    for (std::uint32_t lo = 0xDC00; lo <= 0xDFFF; ++lo) {
      const std::uint32_t cp =
          0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
      const std::string tok = token_of(u4(hi, false) + u4(lo, true));
      EXPECT_TRUE(decode_to(tok, out)) << tok;
      EXPECT_EQ(out, expected_utf8(cp)) << tok;
    }
  }
}

TEST(StringSemantics, NotPairs) {
  // Two highs / two lows and any reversed order never pair — worth pinning
  // the boundary cells of the matrix.
  std::string out;
  EXPECT_TRUE(decode_to(token_of("\\udbff\\udbff"), out));
  EXPECT_EQ(out, "\\udbff\\udbff");
  EXPECT_TRUE(decode_to(token_of("\\udfff\\udfff"), out));
  EXPECT_EQ(out, "\\udfff\\udfff");
  EXPECT_TRUE(decode_to(token_of("\\ud800\\udb00"), out));
  EXPECT_EQ(out, "\\ud800\\udb00"); // mid high after high: both lone
}

TEST(StringUtf8, RawValidity) {
  // Raw non-ASCII bytes must form strict RFC 3629 UTF-8 (P7-1 semantics).
  EXPECT_TRUE(accepted(token_of("\xF4\x8F\xBF\xBF")));    // U+10FFFF
  EXPECT_TRUE(accepted(token_of("\xF0\x90\x80\x80")));    // U+10000
  EXPECT_TRUE(accepted(token_of("\xED\x9F\xBF")));        // U+D7FF
  EXPECT_TRUE(accepted(token_of("\xEE\x80\x80")));        // U+E000
  EXPECT_TRUE(accepted(token_of("\xDF\xBF")));            // U+07FF
  EXPECT_FALSE(accepted(token_of("\xED\xA0\x80")));       // encoded U+D800
  EXPECT_FALSE(accepted(token_of("\xED\xAF\xBF")));       // another surrogate
  EXPECT_FALSE(accepted(token_of("\xC0\xAF")));           // overlong
  EXPECT_FALSE(accepted(token_of("\xE0\x80\x80")));      // overlong 3-byte
  EXPECT_FALSE(accepted(token_of("\xF0\x80\x80\x80")));  // overlong 4-byte
  EXPECT_FALSE(accepted(token_of("\xF4\x90\x80\x80")));  // > U+10FFFF
  EXPECT_FALSE(accepted(token_of("\xFF")));              // illegal lead
  EXPECT_FALSE(accepted(token_of("\x80")));              // stray continuation
  EXPECT_FALSE(accepted(token_of("\xF0\x9F")));          // truncated at end
  EXPECT_FALSE(accepted(token_of("\xE2\x82")));          // truncated at end
}

TEST(StringGate, TruncationPrefixes) {
  // Any strict prefix of a complete valid token is an error (scan hit the
  // buffer end before the closing quote). Prefix errors MID raw UTF-8
  // sequence are still exactly the same scan rule: unclosed == InvalidJson
  // for grammar breaks; mid-multibyte truncations are InvalidUtf8 — here we
  // only pin ACCEPT/REJECT parity per prefix.
  const std::string bodies[] = {
    "abc",
    "ab\\ncd",
    "\\u0041y",
    "\\uD83D\\uDE00",        // full pair token
    "\\ud800y",              // lone with a follower
    "\xF0\x9F\x98\x80raw",   // raw multibyte + plain
    "\\u0000p",
    "",
  };
  for (const std::string &body : bodies) {
    const std::string tok = token_of(body);
    for (std::size_t n = 1; n < tok.size(); ++n) {
      EXPECT_TRUE(scan_string(tok.data(), n).has_error())
          << "prefix len " << n << " of " << tok;
    }
    EXPECT_TRUE(accepted(tok)) << tok;
  }
}

TEST(StringGate, TruncationErrorClass) {
  // Mid-token truncations distinguish grammar from UTF-8 breaks:
  const std::string hex_mid = "\"\\u00"; // raw hex run cut by buffer end
  EXPECT_FALSE(accepted(hex_mid));
  const std::string seq_cut = "\"\xE2\x82"; // raw 3-byte sequence cut
  EXPECT_FALSE(accepted(seq_cut));
  const std::string lead_cut = "\"\xF0";
  EXPECT_FALSE(accepted(lead_cut));
}

// ----------------------------------------------------------------- API

TEST(StringApi, CapBoundaries) {
  const std::string tok = token_of("\\uD83D\\uDE00"); // 12 body -> 4 bytes
  char buf[32];
  std::memset(buf, 0xAB, sizeof(buf));
  auto d = unescape_string(tok.data(), tok.size(), buf, 3);
  ASSERT_TRUE(d.has_error());
  EXPECT_EQ(d.error(), zuu::core::JsonErrc::InsufficientBuffer);
  EXPECT_EQ(buf[0], '\xAB'); // dst untouched on InsufficientBuffer

  d = unescape_string(tok.data(), tok.size(), buf, 4);
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d.value(), 4);
  EXPECT_EQ(buf[0], '\xF0'); // lead byte of the astral point

  // cap = inner_len (upper bound) is always sufficient:
  EXPECT_TRUE(unescape_string(tok.data(), tok.size(), buf, 12).has_value());
  char one;
  EXPECT_TRUE(unescape_string(token_of("x").data(), 3, &one, 1).has_value());
  // An unclosed token must write NOTHING and report InvalidJson:
  std::vector<char> sink(16);
  auto err = unescape_string("\"xyz", 4, sink.data(), sink.size());
  ASSERT_TRUE(err.has_error());
  for (auto c : sink) {
    ASSERT_EQ(0, static_cast<int>(c)); // sink was zeroed, stayed zero
  }
}

TEST(StringApi, PrimingAndErrors) {
  // The error taxonomy is part of the contract: scan reports grammar as
  // InvalidJson and raw UTF-8 breaks as InvalidUtf8.
  const auto g = scan_string("\"ab", 3);
  ASSERT_TRUE(g.has_error());
  EXPECT_EQ(g.error(), zuu::core::JsonErrc::InvalidJson);
  const auto u = scan_string("\"\xFF", 2);
  ASSERT_TRUE(u.has_error());
  EXPECT_EQ(u.error(), zuu::core::JsonErrc::InvalidUtf8);
}

// ---------------------------------------------------- reference differential

/**
 * Independent reference decoder (same locked policy, separately written):
 * linear interior walk, window-based UTF-8 checks, forward-peek pairing.
 * Returns acceptance + the decoded bytes and the consumed token length.
 */
struct RefResult {
  bool ok{false};
  bool utf8_err{false}; // error class parity: InvalidUtf8 vs InvalidJson
  std::string bytes;
  std::size_t consumed{0};
};

[[nodiscard]] RefResult ref_decode(const std::string &tok) {
  RefResult res;
  const std::size_t n = tok.size();
  if (n < 2 || static_cast<std::uint8_t>(tok[0]) != '"') {
    return res;
  }
  std::string out;
  out.reserve(n);
  const auto fail = [&](bool utf8) {
    return RefResult{false, utf8, ""};
  };
  std::size_t i = 1;
  auto hex_at = [&](std::size_t pos, std::uint32_t &value) {
    std::uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = tok[pos + static_cast<std::size_t>(k)];
      std::uint32_t d;
      if (c >= '0' && c <= '9') {
        d = static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        d = static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        d = static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
      v = (v << 4) + d;
    }
    value = v;
    return true;
  };

  while (i < n) {
    const auto b = static_cast<std::uint8_t>(tok[i]);
    if (b == '"') {
      res.ok = true;
      res.bytes = out;
      res.consumed = i + 1;
      return res;
    }
    if (b < 0x20) {
      return fail(false); // raw control
    }
    if (b != '\\') {
      if (b < 0x80) {
        out.push_back(static_cast<char>(b));
        i += 1;
        continue;
      }
      std::size_t need = 0;
      std::uint8_t lo = 0x80;
      std::uint8_t hi = 0xBF;
      if (b >= 0xC2 && b <= 0xDF) {
        need = 2;
      } else if (b == 0xE0) {
        need = 3;
        lo = 0xA0;
      } else if ((b >= 0xE1 && b <= 0xEC) || b == 0xEE || b == 0xEF) {
        need = 3;
      } else if (b == 0xED) {
        need = 3;
        hi = 0x9F;
      } else if (b == 0xF0) {
        need = 4;
        lo = 0x90;
      } else if (b >= 0xF1 && b <= 0xF3) {
        need = 4;
      } else if (b == 0xF4) {
        need = 4;
        hi = 0x8F;
      } else {
        return fail(true); // stray continuation / C0 C1 F5-FF
      }
      if (i + need > n) {
        return fail(true); // truncated by the token end
      }
      const auto b1 = static_cast<std::uint8_t>(tok[i + 1]);
      if (b1 < lo || b1 > hi) {
        return fail(true);
      }
      bool seq_ok = true;
      for (std::size_t k = 2; seq_ok && k < need; ++k) {
        const auto bk = static_cast<std::uint8_t>(tok[i + k]);
        seq_ok = bk >= 0x80 && bk <= 0xBF;
      }
      if (!seq_ok) {
        return fail(true);
      }
      out.append(tok, i, need);
      i += need;
      continue;
    }
    // backslash escape:
    if (i + 1 >= n) {
      return fail(false); // trailing backslash
    }
    const char esc = tok[i + 1];
    char map = 0;
    bool simple = false;
    switch (esc) {
    case '"': map = '"'; simple = true; break;
    case '\\': map = '\\'; simple = true; break;
    case '/': map = '/'; simple = true; break;
    case 'b': map = '\b'; simple = true; break;
    case 'f': map = '\f'; simple = true; break;
    case 'n': map = '\n'; simple = true; break;
    case 'r': map = '\r'; simple = true; break;
    case 't': map = '\t'; simple = true; break;
    default: break;
    }
    if (simple) {
      out.push_back(map);
      i += 2;
      continue;
    }
    if (esc != 'u') {
      return fail(false); // bad escape family
    }
    if (i + 6 >= n) {
      return fail(false); // hex digits would cross the closing quote
    }
    std::uint32_t cp = 0;
    if (!hex_at(i + 2, cp)) {
      return fail(false);
    }
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      std::uint32_t lo = 0;
      if (i + 12 < n && tok[i + 6] == '\\' && tok[i + 7] == 'u'
          && hex_at(i + 8, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        out += expected_utf8(cp);
        i += 12;
        continue;
      }
      out.append(tok, i, 6); // lone high: verbatim pass-through
      i += 6;
      continue;
    }
    if (cp >= 0xDC00 && cp <= 0xDFFF) {
      out.append(tok, i, 6); // lone low: verbatim pass-through
      i += 6;
      continue;
    }
    out += expected_utf8(cp);
    i += 6;
  }
  return fail(false); // ran off the interior without the closing quote
}

/** Agreement of the production layer with the reference. */
void compare_with_ref(const std::string &tok) {
  const auto ref = ref_decode(tok);
  const auto s = scan_string(tok.data(), tok.size());
  const bool impl_ok = s.has_value();
  ASSERT_EQ(impl_ok, ref.ok) << "token: " << tok;
  if (!ref.ok) {
    ASSERT_TRUE(s.has_error());
    if (ref.utf8_err) {
      EXPECT_EQ(s.error(), zuu::core::JsonErrc::InvalidUtf8) << tok;
    } else {
      EXPECT_EQ(s.error(), zuu::core::JsonErrc::InvalidJson) << tok;
    }
    return;
  }
  // Decode bytes must match bitwise:
  std::vector<char> buf(s.value().inner_len + 8);
  const auto d = unescape_string(tok.data(), tok.size(), buf.data(),
                                 buf.size());
  ASSERT_TRUE(d.has_value()) << tok;
  ASSERT_EQ(d.value(), ref.bytes.size()) << tok;
  EXPECT_EQ(std::memcmp(buf.data(), ref.bytes.data(), ref.bytes.size()), 0)
      << "bytes diverge for token: " << tok;
  // Consumed-span contract:
  EXPECT_EQ(s.value().total_len, ref.consumed) << tok;
}

/**
 * Random valid JSON string token (body of plain bytes, simple escapes,
 * \u escapes of every family, surrogate pairs/lone escapes, raw UTF-8).
 */
[[nodiscard]] std::string gen_token_body(std::mt19937_64 &rng,
                                         bool make_pair_rate) {
  std::string body;
  const std::uint32_t units = 1 + rng() % 12;
  for (std::uint32_t u = 0; u < units; ++u) {
    const std::uint32_t kind = rng() % 100;
    if (kind < 30) {
      // plain ASCII (no quote/backslash)
      static const char kPlain[] = "abcXYZ ,.:0;!?morOoe9";
      body += kPlain[rng() % (sizeof(kPlain) - 1)];
    } else if (kind < 45) {
      // simple escape
      static const char kSimples[] = "\"\\/bfnrt";
      body.push_back('\\');
      body.push_back(kSimples[rng() % (sizeof(kSimples) - 1)]);
    } else if (kind < 68) {
      // \u escape of a random code point (both hex cases)
      const std::uint32_t cp = rng() % 0x10000;
      body += u4(cp, (rng() % 2) == 0);
    } else if (kind < 82) {
      // surrogate escape block: often a real pair, sometimes lone
      std::uint32_t hi;
      std::uint32_t lo;
      if (make_pair_rate && (rng() % 3) != 0) {
        hi = 0xD800 + (rng() % 0x400);
        lo = 0xDC00 + (rng() % 0x400);
      } else {
        hi = rng() % 0x10000;
        lo = rng() % 0x10000;
      }
      body += u4(hi, (rng() % 2) == 0);
      body += u4(lo, (rng() % 2) == 0);
    } else {
      // raw UTF-8 of a random valid scalar (skip the surrogate range)
      std::uint32_t cp = 0x80 + (rng() % 0xFF7F); // [0x80..0xFFFFF]
      if (cp >= 0xD800 && cp <= 0xDFFF) {
        cp += 0x800; // dodge the range
      }
      body += expected_utf8(cp);
    }
  }
  return body;
}

TEST(StringFuzzRef, GeneratedTokens) {
  std::mt19937_64 rng(20261110);
  for (int trial = 0; trial < 60000; ++trial) {
    const std::string tok = token_of(gen_token_body(rng, true));
    compare_with_ref(tok);
  }
}

TEST(StringFuzzRef, CorruptedTokens) {
  std::mt19937_64 rng(20261110);
  for (int trial = 0; trial < 60000; ++trial) {
    std::string tok = token_of(gen_token_body(rng, (rng() % 2) == 0));
    // Mutate one byte to a category that terminates/alters the token.
    const std::size_t pos = 1 + rng() % (tok.size() - 1);
    static const std::array<char, 8> kMutations{
      '"', '\\', 'u', '\x01', '\x1F', '\x41', '\xFF', '\x0A'
    };
    tok[pos] = kMutations[rng() % kMutations.size()];
    compare_with_ref(tok); // the closing quote stays as the token end
  }
}

} // namespace

// Custom main: persist the export-helper sidecar (ZE_METADATA_OUT) when the
// run is instrumented, then execute the suite. Plain local runs are no-ops.
int main(int argc, char **argv) {
  zuu::testing::write_export_sidecar("ZE_METADATA_OUT", "ZE_RUN_ID",
                                     "ZE_GIT_COMMIT", "ze_json_string_test");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
