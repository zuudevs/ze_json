/**
 * ze_json.mjs — raw wasm64 C-ABI binding for ze_json (TASKS P3-1).
 *
 * ABI contract: see `src/bindings/cabi.cpp` in the ze_json repository.
 *   - memory is module-EXPORTED (memory64, 64 MiB initial)
 *   - ze_alloc: bump allocator from the linker-defined __heap_base
 *     (no individual free; ze_reset_heap() rewinds between corpora)
 *   - packed scan result: ok => count (high 32 bits zero),
 *     error => (JsonErrc code << 32); dst entries are 64-bit size_t.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_WASM = path.join(HERE, 'ze_json.wasm');

export const ABI_VERSION = 1;

/** Index order matches zuu::core::JsonErrc (core/error.hpp). */
export const JSON_ERRORS = [
  'None', 'EmptyJson', 'InvalidJson', 'UnexpectedQuote',
  'InvalidQuote', 'InsufficientBuffer', 'InvalidUtf8',
];

/** Loaded wasm module + wrapped calls. Synchronous on purpose: the harness
 *  and tests are CLI tools, and module instantiation is deterministic. */
export class ZeJson {
  #exports;
  #memory;

  constructor(exports) {
    this.#exports = exports;
    this.#memory = exports.memory;
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

  scanner8(srcPtr, len, dstPtr, cap) {
    return this.#packed(this.#exports.ze_scanner8, srcPtr, len, dstPtr, cap);
  }

  referenceScan(srcPtr, len, dstPtr, cap) {
    return this.#packed(this.#exports.ze_reference_scan, srcPtr, len, dstPtr, cap);
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

  constructor(binding, bytes) {
    this.#binding = binding;
    this.#len = bytes.length;
    this.#cap = bytes.length; // worst case: one structural index per byte
    this.#srcPtr = binding.alloc(this.#len);
    this.#dstPtr = binding.alloc(this.#cap * 8);
    binding.writeBytes(this.#srcPtr, bytes);
  }

  get byteLength() {
    return this.#len;
  }

  scan8() {
    return this.#binding.scanner8(this.#srcPtr, this.#len, this.#dstPtr, this.#cap);
  }

  referenceScan() {
    return this.#binding.referenceScan(this.#srcPtr, this.#len, this.#dstPtr, this.#cap);
  }

  indices(result) {
    return result.ok ? this.#binding.readIndices(this.#dstPtr, result.count) : [];
  }
}
