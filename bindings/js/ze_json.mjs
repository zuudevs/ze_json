/**
 * ze_json.mjs — raw wasm64 C-ABI binding for ze_json (TASKS P3-1, R2, P7-5).
 *
 * ABI contract: `include/ze_json/abi/cabi.hpp` (single source of truth).
 *   - memory is module-EXPORTED (memory64, 64 MiB initial)
 *   - ze_alloc: bump allocator from the linker-defined __heap_base
 *     (no individual free; ze_reset_heap() rewinds between corpora and
 *     closes every parser slot)
 *   - packed scan result: ok => count (high 32 bits zero),
 *     error => (JsonErrc code << 32); dst entries are 64-bit size_t.
 *   - v2 adds the scanner-mode surface: the caller picks the scanner
 *     variant per scan (structural = the library's measured dispatch).
 *   - v3 adds the on-demand parser surface on a static slot pool
 *     (kParserSlotMax slots, see kParserSlotMax): traversal is stateless
 *     per call — the (cursor, token_start) frame pair a step result
 *     returns is fed back as full-width u64 arguments.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_WASM = path.join(HERE, 'ze_json.wasm');

export const ABI_VERSION = 3;

/** Index order matches zuu::core::JsonErrc (core/error.hpp). */
export const JSON_ERRORS = [
  'None', 'EmptyJson', 'InvalidJson', 'UnexpectedQuote',
  'InvalidQuote', 'InsufficientBuffer', 'InvalidUtf8',
];

/**
 * Scanner modes selectable from JS — mirrors the v2 export table in
 * `include/ze_json/abi/cabi.hpp`. `structural` is the production entry
 * point (the library resolves its measured default once); the explicit
 * variants exist for the thesis comparison matrix and profiling.
 */
export const SCANNER_MODES = Object.freeze({
  structural: 'ze_structural_scan',
  scanner8: 'ze_scanner8',
  scanner64: 'ze_scanner64',
  scanner128: 'ze_scanner128',
  reference: 'ze_reference_scan',
});

/**
 * Node kinds returned by valueKind — mirrors `ondemand::NodeType`
 * (index order in core::kOndemandNodeType, one load per value).
 */
export const NODE_KINDS = Object.freeze([
  'unknown', // 0: launcher byte cannot begin a JSON value (also dead slot)
  'object', // 1
  'array', // 2
  'string', // 3
  'number', // 4
  'true', // 5
  'false', // 6
  'null', // 7
]);

/**
 * Step-family completion sentinel (include/ze_json/abi/cabi.hpp
 * kStepSentinel): `(SEN << 32)` means the container is exhausted — never
 * a valid cursor for a sub-4-GiB document.
 */
export const STEP_SENTINEL = 0xffffffff;

/** Parser slot pool size — mirrors `abi::kParserSlotMax` (cabi.hpp). */
export const PARSER_SLOT_MAX = 64;

/** Loaded wasm module + wrapped calls. Synchronous on purpose: the harness
 *  and tests are CLI tools, and module instantiation is deterministic. */
export class ZeJson {
  #exports;
  #memory;
  #scanFns = new Map();

  constructor(exports) {
    this.#exports = exports;
    this.#memory = exports.memory;
    for (const [mode, exportName] of Object.entries(SCANNER_MODES)) {
      if (typeof exports[exportName] === 'function') {
        this.#scanFns.set(mode, exports[exportName]);
      }
    }
  }

  static load(wasmPath = DEFAULT_WASM) {
    const bytes = fs.readFileSync(wasmPath);
    const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes), {});
    const binding = new ZeJson(instance.exports);
    const version = binding.abiVersion();
    if (version !== ABI_VERSION) {
      throw new Error(`ze_json ABI version mismatch: module ${version}, binding ${ABI_VERSION}`);
    }
    return binding;
  }

  abiVersion() {
    return Number(this.#exports.ze_abi_version());
  }

  /** Production default: the library's static dispatch (scanner8 today). */
  get defaultScanner() {
    return 'structural';
  }

  /** True when this wasm build exports the requested scanner variant. */
  hasScanner(mode) {
    return this.#scanFns.has(mode);
  }

  /** Throws (TypeError / Error) when `mode` is unknown or unavailable. */
  assertScanner(mode) {
    this.#resolveScanner(mode);
  }

  #resolveScanner(mode) {
    if (typeof mode !== 'string' || !(mode in SCANNER_MODES)) {
      throw new TypeError(
        `ze_json: unknown scanner mode ${JSON.stringify(mode)}; expected one of `
        + Object.keys(SCANNER_MODES).map((m) => `'${m}'`).join(', '));
    }
    const fn = this.#scanFns.get(mode);
    if (fn === undefined) {
      throw new Error(
        `ze_json: scanner mode '${mode}' is not available in this wasm build `
        + `(missing export ${SCANNER_MODES[mode]})`);
    }
    return fn;
  }

  /** Bump-allocate `byteLength` bytes (16-byte aligned). No free. */
  alloc(byteLength) {
    return Number(this.#exports.ze_alloc(BigInt(byteLength)));
  }

  /** Rewind the bump allocator (call between corpora, after documents expire). */
  resetHeap() {
    this.#exports.ze_reset_heap();
  }

  writeBytes(ptr, bytes) {
    new Uint8Array(this.#memory.buffer).set(bytes, ptr);
  }

  #packed(scanFn, srcPtr, len, dstPtr, cap) {
    const ret = scanFn(BigInt(srcPtr), BigInt(len), BigInt(dstPtr), BigInt(cap));
    // wasm i64 -> SIGNED BigInt; normalize before decoding packed halves.
    const uRet = BigInt.asUintN(64, ret);
    const code = Number(uRet >> 32n);
    if (code !== 0) {
      return { ok: false, error: JSON_ERRORS[code] ?? `code ${code}`, code };
    }
    return { ok: true, count: Number(uRet & 0xffffffffn) };
  }

  /** Run a scanner by mode name (see SCANNER_MODES). */
  scan(mode, srcPtr, len, dstPtr, cap) {
    return this.#packed(this.#resolveScanner(mode), srcPtr, len, dstPtr, cap);
  }

  // Back-compat aliases (pre-R2 call sites).
  scanner8(srcPtr, len, dstPtr, cap) {
    return this.scan('scanner8', srcPtr, len, dstPtr, cap);
  }

  referenceScan(srcPtr, len, dstPtr, cap) {
    return this.scan('reference', srcPtr, len, dstPtr, cap);
  }

  /** Copy `count` structural indexes (64-bit each on wasm64) out of wasm memory. */
  readIndices(dstPtr, count) {
    const view = new BigUint64Array(this.#memory.buffer, dstPtr, count);
    const indices = new Array(count);
    for (let i = 0; i < count; i += 1) {
      indices[i] = Number(view[i]);
    }
    return indices;
  }

  /** Live view into linear memory at `ptr` (use immediately: a memory grow
   *  detaches the backing buffer). */
  readBytes(ptr, byteLength) {
    return new Uint8Array(this.#memory.buffer, ptr, byteLength);
  }

  // === v3 on-demand parser surface (TASKS P7-5) ============================
  //
  // Stateless per call: a step result's (cursor, tokenStart) frame is fed
  // back as arguments to the NEXT step until it reports done. Packing
  // decoder (include/ze_json/abi/cabi.hpp v3 result packing):
  //   hi === 0                -> ok, payload = low half (semantics per call)
  //   hi === STEP_SENTINEL    -> done (frame family) — never a valid cursor
  //   hi !== 0 && lo === 0    -> error, code = hi (scalar + step families)
  //   else                    -> step frame { cursor: hi, ts: lo }
  #frame(rawRet) {
    // wasm i64 returns arrive as SIGNED BigInt (Emscripten-free raw ABI);
    // normalize to unsigned before decoding (packed msb sentinel then
    // reads back as hi = 0xffffffff, not -1).
    const ret = BigInt.asUintN(64, rawRet);
    const hi = ret >> 32n;
    const lo = ret & 0xffffffffn;
    if (hi === BigInt(STEP_SENTINEL)) {
      return { ok: true, done: true };
    }
    if (hi !== 0n && lo === 0n) {
      return {
        ok: false,
        error: JSON_ERRORS[Number(hi)] ?? `code ${Number(hi)}`,
        code: Number(hi),
      };
    }
    return { ok: true, ts: Number(lo), cursor: Number(hi) };
  }

  #plain(rawRet) {
    const ret = BigInt.asUintN(64, rawRet);
    const hi = Number(ret >> 32n);
    if (hi !== 0) {
      return {
        ok: false,
        error: JSON_ERRORS[hi] ?? `code ${hi}`,
        code: hi,
      };
    }
    return { ok: true, value: Number(ret & 0xffffffffn) };
  }

  /** Bind a scratch region already written to linear memory into a parser
   *  slot: Document::open + the C++ static dispatch scanner. Returns the
   *  slot id (use with every other parser call). */
  openParser(srcPtr, byteLength) {
    const open = this.#plain(
      this.#exports.ze_parser_open(BigInt(srcPtr), BigInt(byteLength)));
    return open.ok ? { ok: true, slot: open.value } : open;
  }

  /** Root token start (re-derive cursor = 0 contract). */
  parserRoot(slot) {
    const root = this.#plain(this.#exports.ze_parser_root(BigInt(slot)));
    return root.ok ? { ok: true, ts: root.value } : root;
  }

  closeParser(slot) {
    this.#exports.ze_parser_close(BigInt(slot));
  }

  /** Plain classification: 0..7 (see NODE_KINDS); 0 = unknown ts/dead slot. */
  valueKind(slot, ts) {
    return Number(this.#exports.ze_value_kind(BigInt(slot), BigInt(ts)));
  }

  /** First child frame of the container at (ts, cursor). */
  containerBegin(slot, ts, cursor) {
    return this.#frame(this.#exports.ze_container_begin(
      BigInt(slot), BigInt(ts), BigInt(cursor)));
  }

  /** ArrayIter++ replay from the current child frame. */
  arrayNext(slot, childTs, childCursor) {
    return this.#frame(this.#exports.ze_array_next(
      BigInt(slot), BigInt(childTs), BigInt(childCursor)));
  }

  /** Next object KEY frame from the current field's value frame. */
  objectNext(slot, valueTs, valueCursor) {
    return this.#frame(this.#exports.ze_object_next(
      BigInt(slot), BigInt(valueTs), BigInt(valueCursor)));
  }

  /** ObjectIter* replay: validate key/':'/launcher, return the VALUE frame. */
  objectField(slot, keyTs, keyCursor) {
    return this.#frame(this.#exports.ze_object_field(
      BigInt(slot), BigInt(keyTs), BigInt(keyCursor)));
  }

  /** Full P7-3 unescape of the string token at `ts` into dst. ok => decoded
   *  byte count (0 = empty); InsufficientBuffer on a too-small cap. */
  getString(slot, ts, dstPtr, dstCap) {
    return this.#plain(this.#exports.ze_get_string(
      BigInt(slot), BigInt(ts), BigInt(dstPtr), BigInt(dstCap)));
  }

  /** V8-parity double read from the 8-byte dst buffer (written LE). */
  getDouble(slot, ts, dstPtr) {
    const result = this.#plain(this.#exports.ze_get_number(
      BigInt(slot), BigInt(ts), BigInt(dstPtr), 8n));
    if (!result.ok) {
      return result;
    }
    const value =
        new DataView(this.#memory.buffer, dstPtr, 8).getFloat64(0, true);
    return { ok: true, value };
  }

  /** Literal check ("true"/"false"); non-literal ts => InvalidJson. */
  getBool(slot, ts) {
    const literal = this.#plain(this.#exports.ze_get_bool(
      BigInt(slot), BigInt(ts)));
    return literal.ok
      ? { ok: true, value: literal.value === 1 }
      : literal;
  }

  /** Plain literal match ("null") — no error channel. */
  isNull(slot, ts) {
    return Number(this.#exports.ze_is_null(BigInt(slot), BigInt(ts))) === 1;
  }

  /** Index-walk framing of container size (skip latitude is intentional). */
  countElements(slot, ts) {
    return this.#plain(this.#exports.ze_count_elements(
      BigInt(slot), BigInt(ts)));
  }
}


/** A document uploaded into wasm memory once, scanned many times. */
export class ZeDocument {
  #binding;
  #srcPtr;
  #dstPtr;
  #len;
  #cap;
  #scanner;

  constructor(binding, bytes, options = {}) {
    this.#binding = binding;
    this.#len = bytes.length;
    this.#cap = bytes.length; // worst case: one structural index per byte
    this.#srcPtr = binding.alloc(this.#len);
    this.#dstPtr = binding.alloc(this.#cap * 8);
    binding.writeBytes(this.#srcPtr, bytes);

    const mode = options.scanner ?? binding.defaultScanner;
    binding.assertScanner(mode); // fail loud at construction time
    this.#scanner = mode;
  }

  get byteLength() {
    return this.#len;
  }

  /** Scanner mode used by scan() when no override is passed. */
  get scanner() {
    return this.#scanner;
  }

  scan(mode) {
    return this.#binding.scan(
      mode === undefined ? this.#scanner : mode,
      this.#srcPtr, this.#len, this.#dstPtr, this.#cap);
  }

  // Back-compat aliases (pre-R2 call sites).
  scan8() {
    return this.scan('scanner8');
  }

  referenceScan() {
    return this.scan('reference');
  }
  indices(result) {
    return result.ok ? this.#binding.readIndices(this.#dstPtr, result.count) : [];
  }
}

/**
 * Error thrown when the on-demand walk surfaces a framing/grammar verdict
 * ("document reads broken") or a contract violation in the driver. Carries
 * the ABI error name (`core::JsonErrc` mirror) when it originates from an
 * export result.
 */
export class ZeParserError extends Error {
  constructor(message, code = undefined) {
    super(message);
    this.name = 'ZeParserError';
    this.code = code;
  }
}

/**
 * ZeParser — one wasm document parsed ON-DEMAND through the v3 C-ABI
 * (TASKS P7-5). The binding stays JS-agnostic: the low-level calls live on
 * ZeJson, this wrapper owns the slot, the source scratch buffer and the
 * internal decode buffers (all carved from the shared bump heap).
 *
 * Lifetime: `close()` releases the slot. Its buffers remain committed until
 * `binding.resetHeap()` (carve is bump-only), which ALSO closes the slot —
 * call close() first or simply drop the instance before rewinding.
 *
 * parse() materializes the FULL document into a JS value tree with
 * V8 `JSON.parse` verdict-and-shape semantics:
 *   - objects are null-prototype; keys are assigned by plain data-property
 *     assignment ("__proto__" stays an OWN key, duplicates = last-wins) —
 *     JSON.parse's DefineOwnProperty semantics via the null-prototype
 *     receiver (no setter on the chain, see #readObject);
 *   - numbers are the exact P7-2 doubles (bit-identical, -0 preserved);
 *   - strings go through the P7-3 decoder; lone-surrogate escapes are
 *     passed through as their literal 6-byte escape text (the lossless
 *     encoding decision) and are therefore NOT byte-equal to a V8 string
 *     built from the same escape — a documented wrapper-boundary semantic.
 *
 * @cite 10.1002_spe.3313 on-demand JSON — the value tree is materialized
 *       by replaying the library's single forward iterator; unvisited
 *       subtrees pay nothing.
 */
export class ZeParser {
  #binding;
  #slot;
  #srcPtr;
  #numPtr;
  #dstPtr;
  #dstCap;
  #rootTs;
  #closed = false;

  constructor(binding, { slot, srcPtr, numPtr, dstPtr, dstCap, rootTs }) {
    this.#binding = binding;
    this.#slot = slot;
    this.#srcPtr = srcPtr;
    this.#numPtr = numPtr;
    this.#dstPtr = dstPtr;
    this.#dstCap = dstCap;
    this.#rootTs = rootTs;
  }

  /** Copy `bytes` into wasm memory and open a parser slot over them. */
  static open(binding, bytes) {
    const srcPtr = binding.alloc(bytes.length);
    binding.writeBytes(srcPtr, bytes);
    const open = binding.openParser(srcPtr, bytes.length);
    if (!open.ok) {
      throw new ZeParserError(`openParser: ${open.error}`, open.code);
    }
    let root;
    try {
      root = binding.parserRoot(open.slot);
    } finally {
      if (!root?.ok) {
        binding.closeParser(open.slot);
      }
    }
    if (!root.ok) {
      throw new ZeParserError(`parserRoot: ${root.error}`, root.code);
    }
    return new ZeParser(binding, {
      slot: open.slot,
      srcPtr,
      numPtr: binding.alloc(16), // 8-byte f64 travel buffer (dst contract)
      dstCap: bytes.length + 1, // inner_len <= total_len <= document length
      dstPtr: binding.alloc(bytes.length + 1),
      rootTs: root.ts,
    });
  }

  get slot() {
    return this.#slot;
  }

  get rootTs() {
    this.#assertOpen();
    return this.#rootTs;
  }

  /** Plain launcher classification at `ts` (NODE_KINDS index). */
  kind(ts) {
    this.#assertOpen();
    return this.#binding.valueKind(this.#slot, ts);
  }

  /** Index-walk child count at a container ts (framing verdict included). */
  countElements(ts) {
    this.#assertOpen();
    const count = this.#binding.countElements(this.#slot, ts);
    if (!count.ok) {
      throw new ZeParserError(`countElements: ${count.error}`, count.code);
    }
    return count.value;
  }

  /** Full P7-3 decode of the string token at `ts` into a JS string. */
  getString(ts) {
    this.#assertOpen();
    return Buffer.from(this.#decodeStringBytes(ts)).toString('utf8');
  }

  /** Exact double at `ts` (V8-locked rounding, -0 preserved). */
  getDouble(ts) {
    this.#assertOpen();
    return this.#readDouble(ts);
  }

  /** Literal read: true/false/null — null => { isNull: true }. */
  getBool(ts) {
    this.#assertOpen();
    const literal = this.#binding.getBool(this.#slot, ts);
    if (!literal.ok) {
      throw new ZeParserError(`getBool: ${literal.error}`, literal.code);
    }
    return literal.value;
  }

  isNull(ts) {
    this.#assertOpen();
    return this.#binding.isNull(this.#slot, ts);
  }

  /** Frame of the first child of the container at (ts, cursor), or null
   *  when the container is empty ({ done } packing decoded). */
  containerBegin(ts, cursor) {
    this.#assertOpen();
    const begin = this.#binding.containerBegin(this.#slot, ts, cursor);
    if (!begin.ok) {
      throw new ZeParserError(
        `containerBegin: ${begin.error}`, begin.code);
    }
    return begin.done ? null : begin;
  }

  /** ArrayIter++ replay; null when the array is exhausted. */
  arrayNext(childTs, childCursor) {
    this.#assertOpen();
    const next = this.#binding.arrayNext(this.#slot, childTs, childCursor);
    if (!next.ok) {
      throw new ZeParserError(`arrayNext: ${next.error}`, next.code);
    }
    return next.done ? null : next;
  }

  /** ObjectIter* replay from the current KEY frame: value frame. */
  objectField(keyTs, keyCursor) {
    this.#assertOpen();
    const field = this.#binding.objectField(this.#slot, keyTs, keyCursor);
    if (!field.ok) {
      throw new ZeParserError(`objectField: ${field.error}`, field.code);
    }
    return field;
  }

  /** ObjectIter++ replay from the current VALUE frame: next key frame or
   *  null when the object is exhausted. */
  objectNext(valueTs, valueCursor) {
    this.#assertOpen();
    const next = this.#binding.objectNext(this.#slot, valueTs, valueCursor);
    if (!next.ok) {
      throw new ZeParserError(`objectNext: ${next.error}`, next.code);
    }
    return next.done ? null : next;
  }

  /** Release the slot (idempotent; buffers wait for resetHeap). */
  close() {
    if (!this.#closed) {
      this.#binding.closeParser(this.#slot);
      this.#closed = true;
    }
  }

  /**
   * Materialize the entire document as a JS value (parse() returns it; the
   * low-level frame calls above keep the walk observable for tooling).
   */
  parse() {
    this.#assertOpen();
    return this.#materialize(this.#rootTs, 0);
  }

  #assertOpen() {
    if (this.#closed) {
      throw new ZeParserError('parser is closed');
    }
  }

  // === value-tree materialization (V8-shape) ===============================

  #materialize(ts, cursor) {
    const kind = this.#binding.valueKind(this.#slot, ts);
    switch (kind) { // NODE_KINDS index
      case 1: return this.#readObject(ts, cursor);
      case 2: return this.#readArray(ts, cursor);
      case 3: return Buffer.from(this.#decodeStringBytes(ts)).toString('utf8');
      case 4: return this.#readDouble(ts);
      case 5: return true;
      case 6: return false;
      case 7: return null;
      default:
        throw new ZeParserError(
          `token at ${ts} is not a JSON value (kind ${kind})`);
    }
  }

  #readDouble(ts) {
    const result = this.#binding.getDouble(this.#slot, ts, this.#numPtr);
    if (!result.ok) {
      throw new ZeParserError(`getDouble: ${result.error}`, result.code);
    }
    return result.value;
  }

  /** Decoded bytes for the string token at `ts` (self-growing dst buffer;
   *  growth carves from the bump heap until the next resetHeap). */
  #decodeStringBytes(ts) {
    while (true) {
      const result =
          this.#binding.getString(this.#slot, ts, this.#dstPtr, this.#dstCap);
      if (result.ok) {
        return this.#binding.readBytes(this.#dstPtr, result.value);
      }
      if (result.code === JSON_ERRORS.indexOf('InsufficientBuffer')) {
        this.#dstCap *= 2;
        this.#dstPtr = this.#binding.alloc(this.#dstCap);
        continue; // unescape left dst untouched — safe to retry
      }
      throw new ZeParserError(`getString: ${result.error}`, result.code);
    }
  }

  #readArray(rootTs, rootCursor) {
    const begin = this.#binding.containerBegin(
      this.#slot, rootTs, rootCursor);
    if (!begin.ok) {
      throw new ZeParserError(
        `containerBegin(array): ${begin.error}`, begin.code);
    }
    if (begin.done) {
      return [];
    }
    // No pre-size pass: count_elements() replays the WHOLE subtree (one
    // begin_array + one ArrayIter step per child) BEFORE the walk that
    // materializes it. The materialization walk re-derives every framing
    // verdict with the same machinery (arrayNext frames each delimiter;
    // scalar contents validate on access), so dropping the count pass
    // loses no coverage — only the internal length-mismatch assert.
    // push() rides V8's amortized PACKED_ELEMENTS growth (R5-2 sandwich:
    // <pending measurement reference>).
    const arr = [];
    let frame = begin;
    while (true) {
      arr.push(this.#materialize(frame.ts, frame.cursor));
      const next = this.#binding.arrayNext(
        this.#slot, frame.ts, frame.cursor);
      if (!next.ok) {
        throw new ZeParserError(
          `arrayNext: ${next.error}`, next.code);
      }
      if (next.done) {
        break;
      }
      frame = next; // { cursor, ts } — exact continuation pair
    }
    return arr;
  }

  #readObject(rootTs, rootCursor) {
    const begin = this.#binding.containerBegin(
      this.#slot, rootTs, rootCursor);
    if (!begin.ok) {
      throw new ZeParserError(
        `containerBegin(object): ${begin.error}`, begin.code);
    }
    const obj = Object.create(null); // JSON.parse owns-no-inherited-keys
    if (begin.done) {
      return obj;
    }
    let frame = begin; // first KEY frame { cursor: ':' index, ts: key ts }
    while (true) {
      // The C++ deref validates key token + ':' + value launcher in one
      // step; the key is decoded only after that gate passes (source side
      // of an invalid field never reaches assignment).
      const field = this.#binding.objectField(this.#slot, frame.ts, frame.cursor);
      if (!field.ok) {
        throw new ZeParserError(`objectField: ${field.error}`, field.code);
      }
      const key = Buffer.from(
        this.#decodeStringBytes(frame.ts)).toString('utf8');
      const value = this.#materialize(field.ts, field.cursor);
      // Plain assignment on the null-prototype target is exactly JSON.parse's
      // DefineOwnProperty semantics: the receiver has no prototype, so
      // `__proto__` finds no setter on the chain and stays an OWN data key;
      // duplicate keys overwrite last-wins at their first-occurrence
      // position; integer-index-like strings follow OrdinaryOwnPropertyKeys
      // on both engines. Assignment rides V8's fast data-property path,
      // while Object.defineProperty takes the generic slow path per field
      // (R5-1 sandwich, dev window 2026-11-11: full-parse median 1.09-1.25x
      // across all six corpora, see the thesis workspace
      // reports/R5-1-materialize-assign.md).
      obj[key] = value;
      const next = this.#binding.objectNext(
        this.#slot, field.ts, field.cursor);
      if (!next.ok) {
        throw new ZeParserError(
          `objectNext: ${next.error}`, next.code);
      }
      if (next.done) {
        break;
      }
      frame = next; // next key frame { cursor, ts }
    }
    return obj;
  }
}

