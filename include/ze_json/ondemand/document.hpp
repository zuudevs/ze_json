#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/core/result.hpp>
#include <ze_json/ondemand/arena.hpp>
#include <ze_json/ondemand/value.hpp>

namespace zuu::ondemand {

/**
 * @brief A parsed-on-demand JSON document backed by a borrowed arena.
 *
 * Document owns NOTHING except references: the source buffer stays with
 * the caller, the structural index lives in the caller-supplied
 * `ondemand::Arena` slice allocated at open (upper bound: `src_size`
 * entries — every indexed structural byte consumes at least one source
 * byte). Lifetime contract: source AND arena must both outlive the
 * document and every `Value`/iterator handed out by it.
 *
 * Structural scanning happens ONCE at open via the library's static
 * dispatch (`structural_scan`); the paper's model puts selection cost at
 * configuration time, per-document overhead is one linear index write.
 *
 * @cite 10.14778_3115404.3115416 Mison — the structural index is the only
 *       document-level representation; string/number tokens are re-scanned
 *       from source on materialization.
 * @cite 10.1002_spe.3313 on-demand JSON (Keiser, Lemire) — DOM-shaped
 *       interface over a single forward iterator; contents of subtrees the
 *       program never visits stay unvalidated.
 */
class Document {
public:
  Document() NOEXCEPT : arena_(nullptr),
                        src_(nullptr),
                        src_size_(0),
                        indexes_(nullptr),
                        index_count_(0) {}
  ~Document() NOEXCEPT = default;
  Document(const Document &) = delete;
  Document &operator=(const Document &) = delete;
  Document(Document &&other) NOEXCEPT = default;
  Document &operator=(Document &&other) NOEXCEPT = default;

  /**
   * @brief Run the structural scan and bind the index into the arena.
   *
   * The scan itself only classifies brackets/commas/colons outside
   * strings (same contract as `reference_scan`: index-for-index). Scalar
   * grammars and UTF-8 inside strings stay UNVALIDATED here — they are
   * promoted by first access (the on-demand contract).
   *
   * A bracket-MATCH table is built in the same linear pass: the LIFO
   * opener/closer pairing for the structural stream (opening index ->
   * matching closer, `kNoBracket` for unmatched). Type-blind depth
   * counting is NOT sound for mixed `{`/`[` containers (the cross-type
   * `[ { ] }` shape walks a one-counter into nonsense), so pairing is the
   * staged representation the skip machinery consumes.
   *
   * Allocation: three arena blocks of at most `src_size *
   * sizeof(core::size_t)` each (index entries, match table, pairing
   * stack). Returns EmptyJson for `src_size == 0`, InvalidJson for a
   * null source — the scanner contract is propagated as-is.
   */
  [[nodiscard]] static core::Result<Document, core::JsonErrc>
  open(ondemand::Arena &arena, const char *src, core::size_t src_size) NOEXCEPT;

  /**
   * @brief The root value (first non-whitespace token).
   *
   * Root framing is validated EAGERLY here (the document boundary is not
   * inside any container): a root scalar must be followed by whitespace
   * only, a root container must own the WHOLE structural index walk from
   * its opener to the last index. Empty/whitespace-only source ->
   * EmptyJson.
   */
  [[nodiscard]] core::Result<Value, core::JsonErrc> get_root() const NOEXCEPT;

  [[nodiscard]] core::size_t index_count() const NOEXCEPT {
    return index_count_;
  }

  // -- internal surface shared with Value/iterators (implemented in ----------
  // -- document.cpp; public so the on-demand object graph stays header-only).
  [[nodiscard]] const char *src() const NOEXCEPT { return src_; }
  [[nodiscard]] core::size_t src_size() const NOEXCEPT { return src_size_; }
  [[nodiscard]] bool opened() const NOEXCEPT { return src_ != nullptr; }
  [[nodiscard]] core::size_t index_at(core::size_t i) const NOEXCEPT {
    return indexes_[i];
  }
  //! Matching closer's index of the bracket-open `opener` (kNoBracket for
  //! unmatched — every consumer must treat it as a framing violation).
  [[nodiscard]] core::size_t index_close(core::size_t opener) const NOEXCEPT {
    return match_[opener];
  }
  //! Byte classification via the generated launcher table (0 = not a value).
  [[nodiscard]] core::uint8_t classify(core::size_t byte_pos) const NOEXCEPT;

private:
  ondemand::Arena *arena_;
  const char *src_;
  core::size_t src_size_;
  const core::size_t *indexes_;
  core::size_t index_count_;
  const core::size_t *match_;
};

/**
 * @brief The LIFO bracket-pairing pass over a structural index stream.
 *
 * ND-13 decomposition: `Document::open` runs this pass in place (single
 * source of truth), and the C-ABI surface exposes it (`ze_match_pass`)
 * so a foreign harness can time the match sub-stage of `s2-open`
 * standalone. wasm64 is freestanding (no clock inside the module), so
 * sub-stage timing happens from the caller's clock over equivalent
 * standalone calls; the fused `ze_parser_open` stays the production
 * bound reported beside them.
 *
 * Semantics (open's inlined history, verbatim): an opener pushes its
 * index; a closer pairs with the TOP only when the TYPE matches — the
 * crossed-closer case (`[ { ] }`) marks BOTH ends `kNoBracket` and keeps
 * walking. Marking (not erroring) keeps the pass structural-knowledge-
 * ONLY; every consumer sees `kNoBracket` as its framing violation.
 *
 * Bounds: every `indexes[j]` must be `< src_size` (the scanner contract;
 * the guard also makes the foreign-facing export safe against garbage
 * indexes — it never fires on scanner output). `match` receives one
 * entry per index; `stack` is LIFO scratch holding at most
 * `index_count` openers; both caller-owned.
 *
 * @return paired-closer count (a closer pairs when the normal path runs;
 *         crossed-closer and opener-less closers stay `kNoBracket`).
 * @cite 10.14778_3115404.3115416 Mison — staging the pairing pass over
 *       the flat structural index stream.
 */
[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
match_pass(const char *src, core::size_t src_size,
           const core::size_t *indexes, core::size_t index_count,
           core::size_t *match, core::size_t *stack) NOEXCEPT;

} // namespace zuu::ondemand
