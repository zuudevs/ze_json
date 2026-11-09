/**
 * ze_json.mjs — raw wasm64 C-ABI binding for ze_json (TASKS P3-1, R2).
 *
 * ABI contract: `include/ze_json/abi/cabi.hpp` (single source of truth).
 *   - memory is module-EXPORTED (memory64, 64 MiB initial)
 *   - ze_alloc: bump allocator from the linker-defined __heap_base
 *     (no individual free; ze_reset_heap() rewinds between corpora)
 *   - packed scan result: ok => count (high 32 bits zero),
 *     error => (JsonErrc code << 32); dst entries are 64-bit size_t.
 *   - v2 adds the scanner-mode surface: the caller picks the scanner
 *     variant per scan (structural = the library's measured dispatch).
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_WASM = path.join(HERE, 'ze_json.wasm');

export const ABI_VERSION = 2;

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
    const code = Number(ret >> 32n);
    if (code !== 0) {
      return { ok: false, error: JSON_ERRORS[code] ?? `code ${code}`, code };
    }
    return { ok: true, count: Number(ret & 0xffffffffn) };
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
