// ============================================================================
//  ze_json.mjs — PUBLIC API JAVASCRIPT untuk ze_json_core.wasm
// ============================================================================
//  Binding Raw WebAssembly (64-bit memory64, tanpa Emscripten) atas API C
//  ABI yang didefinisikan include/ze_json/ze_json.hpp.
//
//  Build yang dibutuhkan:
//      cmake --preset wasm64-release
//      cmake --build build/wasm64-release
//    → build/wasm64-release/src/ze_json_core.wasm
//
//  Pemakaian:
//      import { ZeJson } from "./js/ze_json.mjs";
//      const ze = await ZeJson.load();            // opsi: path/binary kustom
//      const doc = ze.openDocument('{"id":21}');
//      doc.find("id");     // → { found: true, type: "Number" }
//      ze.intAt(doc, "id") .value;                // → 21 (satu panggilan wasm)
//
//  Konsekuensi ABI wasm64: semua pointer & size dari JS berupa BigInt.
//  Konsekuensi API minimal: 1 Session = 1 dokumen JSON ber-root OBJECT.
//  Runtime Node.js 24 sudah aktif memory64; versi lebih tua:
//      node --experimental-wasm-memory64
// ============================================================================

import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const MODULE_DIR = dirname(fileURLToPath(import.meta.url));
const PACKAGE_ROOT = resolve(MODULE_DIR, "..");

// Kandidat default ze_json_core.wasm, berurutan:
//   1. bersebelahan dengan modul — bentuk paket npm ter-publish
//      (dibawa "files" js/package.json oleh prepack)
//   2. build/wasm64-release/... — pengembangan di dalam repo
const WASM_CANDIDATES = [
  join(MODULE_DIR, "ze_json_core.wasm"),
  join(PACKAGE_ROOT, "build/wasm64-release/src/ze_json_core.wasm"),
];

// ---------------------------------------------------------------------------
//  KONSTANTA ABI — sinkron dengan include/ze_json/ze_json.hpp (diekspor agar
//  pemakai low-level bisa memakai slot yang sama)
// ---------------------------------------------------------------------------

/** Nama kode status C ABI (indeks = nilai enum Status). */
export const STATUS = [
  "OK", "EMPTY_JSON", "INVALID_JSON", "UNEXPECTED_QUOTE",
  "INVALID_QUOTE", "INSUFFICIENT_BUFFER", "INVALID_UTF8",
  "NOT_FOUND", "TYPE_MISMATCH", "INVALID_PATH",
];

/** Nama tipe nilai C ABI (indeks = nilai enum ValueType). */
export const VALUE_TYPE = ["Array", "Object", "String", "Number", "Boolean", "Null"];

// LAYOUT MEMORY (kontrak tetap):
//   [0x0000] scratch : slot out-param ABI
//                      0x00 int32 error │ 0x08 u64 data │ 0x10 u64 len
//   [0x0020] keys    : nama field/path untuk find_field/find_path
//   [0x0400] payload : byte JSON
//   [0x...  ] tape   : posisi struktural (1 MiB = 131.072 entri)
//   [0x...  ] arena  : Session + string unescape

export const PAGE_SIZE = 65536n; // 64 KiB — ukuran halaman WebAssembly

export const ERR_REF = 0x00n;  // int32 slot: kode status (out-param)
export const DATA_REF = 0x08n; // u64 slot: pointer data string (out-param)
export const LEN_REF = 0x10n;  // u64 slot: panjang string (out-param)

export const KEY_BASE = 0x0020n;
export const KEY_CAP = 512n;

export const PAYLOAD_BASE = 0x0400n;
export const PAYLOAD_CAP = 1024n * 1024n;

export const TAPE_BASE = PAYLOAD_BASE + PAYLOAD_CAP; // align-i64 aman
export const TAPE_BYTES = PAYLOAD_CAP;
export const TAPE_ENTRIES = PAYLOAD_CAP / 8n; // ABI minta SATUAN ENTRI!

export const ARENA_BASE = TAPE_BASE + TAPE_BYTES;
export const ARENA_CAP = 1024n * 1024n;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

// Pencarian byte manual — Uint8Array.prototype.indexOf tidak menerima string,
// sehingga pencarian pola teks pada memory wasm harus meloloskan manual ini.
/**
 * Cari kemunculan pertama `text` (UTF-8) pada `hay` mulai `start`.
 * Mengembalikan indeks byte atau -1.
 */
export function indexOfBytes(hay, start, text) {
  const needle = encoder.encode(text);
  outer: for (let i = start; i <= hay.length - needle.length; i++) {
    for (let k = 0; k < needle.length; k++) {
      if (hay[i + k] !== needle[k]) continue outer;
    }
    return i;
  }
  return -1;
}

// ---------------------------------------------------------------------------
//  WRAPPER — menerjemahkan C ABI wasm64 (BigInt) ke bentuk ergonomis
// ---------------------------------------------------------------------------

export class ZeJson {
  /**
   * Muat ze_json_core.wasm dan bangun wrapper + region memori tetap.
   *
   * @param {object} [options]
   * @param {string} [options.wasmPath]    — path file .wasm kustom;
   *     default: kandidat bersebelahan modul, lalu build/wasm64-release
   * @param {Uint8Array} [options.binary]  — binary .wasm jadi (browser /
   *     pemakai bundler); mengabaikan wasmPath bila diisi
   * @returns {Promise<ZeJson>}
   */
  static async load({ wasmPath, binary } = {}) {
    let bytes = binary ?? null;
    if (!bytes) {
      for (const path of wasmPath ? [wasmPath] : WASM_CANDIDATES) {
        try {
          bytes = readFileSync(path);
          break;
        } catch {
          // kandidat berikutnya
        }
      }
      if (!bytes) {
        throw new Error(
          "ze_json_core.wasm tidak ditemukan — build dulu:\n" +
            "    cmake --preset wasm64-release\n" +
            "    cmake --build build/wasm64-release",
        );
      }
    }

    let instance;
    try {
      ({ instance } = await WebAssembly.instantiate(bytes, {}));
    } catch (err) {
      // memory64 mungkin belum aktif pada runtime JS ini
      throw new Error(
        `gagal kompilasi wasm: ${err.message}\n` +
          "Coba: node --experimental-wasm-memory64",
      );
    }
    return new ZeJson(instance.exports);
  }

  /** Validasi export section; wasm lama/tidak sinkron → error jelas. */
  constructor(ex) {
    for (const name of [
      "create_arena", "reset_arena", "get_arena_used_space",
      "create_session", "find_field", "array_size", "array_next",
      "find_path", "get_int_at", "get_double_at", "get_bool_at",
      "get_string_at", "value_type",
      "get_int", "get_double", "get_bool", "get_string",
    ]) {
      if (typeof ex[name] !== "function") {
        throw new Error(
          `export '${name}' hilang — wasm tidak sinkron dengan ze_json.hpp`,
        );
      }
    }
    this.ex = ex;

    // (1) Grow memory SEKALI — alamat region tidak berubah setelah ini.
    this.ensureCapacity(ARENA_BASE + ARENA_CAP);

    // (2) Arena dialokasi dari region tetap → handle selalu sama.
    this.arena = ex.create_arena(ARENA_BASE, ARENA_CAP);
    if (this.arena === 0n) throw new Error("create_arena gagal");
  }

  // --- siklus hidup ---------------------------------------------------------

  /** Scan + bangun Session untuk satu dokumen; memakai ulang seluruh region. */
  openDocument(jsonText) {
    const [src, srcSize] = this.writeText(PAYLOAD_BASE, PAYLOAD_CAP, jsonText);
    return this.#openRaw(src, srcSize);
  }

  /** Buang Session & unescape-string sebelumnya; wajib seanti dokumen baru. */
  resetArena() {
    this.ex.reset_arena(this.arena);
  }

  /** Byte arena yang terpakai (BigInt i64). */
  arenaUsed() {
    return this.ex.get_arena_used_space(this.arena);
  }

  /** Rilis arena — pemanggilan TERAKHIR di siklus hidup wrapper ini. */
  close() {
    this.ex.destroy_arena(this.arena);
    this.arena = 0n;
  }

  #openRaw(src, srcSize) {
    this.resetArena(); // buang Session & string unescape dokumen sebelumnya

    const session = this.ex.create_session(
      src, srcSize, TAPE_BASE, TAPE_ENTRIES, this.arena, ERR_REF);

    if (session === 0n) {
      const code = new DataView(this.buffer()).getInt32(Number(ERR_REF), true);
      throw new Error(`scan gagal: STATUS=${STATUS[code] ?? code}`);
    }
    return new Doc(this, session);
  }

  // --- memori ---------------------------------------------------------------

  buffer() {
    return this.ex.memory.buffer;
  }

  u8() {
    return new Uint8Array(this.buffer());
  }

  ensureCapacity(bytes) {
    const current = BigInt(this.buffer().byteLength);
    if (bytes <= current) return;
    const pages = (bytes - current + PAGE_SIZE - 1n) / PAGE_SIZE;
    this.ex.memory.grow(pages); // memory64: grow(BigInt halaman)
  }

  // --- tulis region --------------------------------------------------

  /** Tulis byte mentah; mengembalikan (ptr, size) BigInt.
   *  Satu byte 0 disisipkan SETELAH payload sebagai pengaman agar parser
   *  angka tidak membaca melewati batas region ketika teks bersebelahan. */
  writeRegion(base, cap, bytes) {
    if (BigInt(bytes.length) + 1n > cap) {
      throw new Error(`region terlalu kecil: butuh ${bytes.length + 1} byte`);
    }
    const baseNumber = Number(base);
    const u8 = this.u8();
    u8.set(bytes, baseNumber);
    u8[baseNumber + bytes.length] = 0;
    return [base, BigInt(bytes.length)];
  }

  writeText(base, cap, text) {
    return this.writeRegion(base, cap, encoder.encode(text));
  }

  // --- API find-on-demand ------------------------------------------------

  find(doc, key) {
    const [keyPtr, keySize] = this.writeText(KEY_BASE, KEY_CAP, key);
    // find_field mengembalikan i32 → Number (0/1), BUKAN BigInt
    return this.ex.find_field(doc.session, keyPtr, keySize) === 1;
  }

  /** Tipe nilai yang sedang dipegang kursor. */
  typeOf(doc) {
    return VALUE_TYPE[Number(this.ex.value_type(doc.session))] ?? "?";
  }

  /** String ZERO-COPY: data menunjuk langsung ke payload / arena.
   *  WAJIB antara resetArena berikutnya. */
  stringOf(doc) {
    this.ex.get_string(doc.session, DATA_REF, LEN_REF);
    return this.readSpan();
  }

  // Getter berikut hanya valid SETELAH find() sukses dan tipe cocok.
  intOf(doc) {
    return this.ex.get_int(doc.session); // i64 → BigInt
  }

  doubleOf(doc) {
    return this.ex.get_double(doc.session); // f64 → number
  }

  boolOf(doc) {
    return this.ex.get_bool(doc.session) === 1;
  }

  // ------------------------------------------------------------------
  //  Traversal array — DUA pola:
  //  (a) Counted: arraySize() sekali saat kursor di '[' lalu loop
  //      arrayNext() (kontrak lama).
  //  (b) Streaming: langsung loop arrayNext() tanpa arraySize() — satu
  //      lintasan pasar tape; hasil identik.
  //  Elemen yang TIDAK dibaca getter di-skip otomatis oleh arrayNext.
  // ------------------------------------------------------------------
  arraySize(doc) {
    return Number(this.ex.array_size(doc.session)); // size_t → BigInt
  }

  arrayNext(doc) {
    return this.ex.array_next(doc.session) === 1; // i32 → Number
  }

  // ------------------------------------------------------------------
  //  PATH QUERY — SATU panggilan wasm per kueri: navigasi penuh
  //  ("position.x", "colors[1]", "items[0].name") + cek tipe +
  //  pembacaan nilai, SEMUA di dalam C++ (kursor selalu di-rewind).
  // ------------------------------------------------------------------
  status() {
    return STATUS[
      new DataView(this.buffer()).getInt32(Number(ERR_REF), true)];
  }

  intAt(doc, path) {
    const [ptr, len] = this.writeText(KEY_BASE, KEY_CAP, path);
    const value = this.ex.get_int_at(doc.session, ptr, len, ERR_REF);
    return { status: this.status(), value: Number(value) };
  }

  doubleAt(doc, path) {
    const [ptr, len] = this.writeText(KEY_BASE, KEY_CAP, path);
    const value = this.ex.get_double_at(doc.session, ptr, len, ERR_REF);
    return { status: this.status(), value };
  }

  boolAt(doc, path) {
    const [ptr, len] = this.writeText(KEY_BASE, KEY_CAP, path);
    const value = this.ex.get_bool_at(doc.session, ptr, len, ERR_REF);
    return { status: this.status(), value };
  }

  stringAt(doc, path) {
    const [ptr, len] = this.writeText(KEY_BASE, KEY_CAP, path);
    this.ex.get_string_at(doc.session, ptr, len, DATA_REF, LEN_REF, ERR_REF);
    return { status: this.status(), value: this.readSpan() };
  }

  // Navigasi path saja (nilai dibaca terpisah: dua panggilan lintas ABI).
  typeAt(doc, path) {
    const [ptr, len] = this.writeText(KEY_BASE, KEY_CAP, path);
    const code = this.ex.find_path(doc.session, ptr, len);
    if (code !== 0) return { status: STATUS[code] ?? code, type: null };
    return { status: "OK", type: this.typeOf(doc) };
  }

  // --- internal ---------------------------------------------------------

  /** Baca span (data,len) hasil out-param terakhir; decode UTF-8 → String. */
  readSpan() {
    const view = new DataView(this.buffer());
    const data = view.getBigUint64(Number(DATA_REF), true);
    const len = view.getBigUint64(Number(LEN_REF), true);
    if (data === 0n) return "";
    return decoder.decode(
      new Uint8Array(this.buffer(), Number(data), Number(len)));
  }
}

/**
 * Kursor dokumen: hasil openDocument(). Menyimpan session (pointer C ABI,
 * BigInt) dan delegasi ke wrapper induknya.
 */
export class Doc {
  constructor(host, session) {
    this.host = host;
    this.session = session; // pointer C ABI (BigInt)
  }

  /** Cari field pada object root → { found, type }. */
  find(key) {
    if (!this.host.find(this, key)) return { found: false };
    return { found: true, type: this.host.typeOf(this) };
  }

  // Getter — valid hanya setelah find() sukses dan tipe sesuai.
  typeOf() { return this.host.typeOf(this); }
  asString() { return this.host.stringOf(this); }
  asInt() { return this.host.intOf(this); }
  asDouble() { return this.host.doubleOf(this); }
  asBoolean() { return this.host.boolOf(this); }

  // Traversal array — dua langkah kontraktor: arraySize() sekali saat
  // kursor di '[', lalu loop arrayNext() (elemen tak-dibaca di-skip
  // otomatis; panggil terakhir mengembalikan kursor ke irama orang tua).
  arraySize() { return this.host.arraySize(this); }
  arrayNext() { return this.host.arrayNext(this); }

  /** Konsumsi literal null (1 entry tape) agar kursor maju — tanpa ini,
   *  find() berikutnya akan salah mem-parse karakter 'n' tersisa. */
  consumeNull() {
    this.host.ex.get_bool(this.session); // advance kursor lewat 'n'
    return null;
  }

  // Path query — delegasi langsung; semua meng-rewind kursor dari root.
  intAt(path) { return this.host.intAt(this, path); }
  doubleAt(path) { return this.host.doubleAt(this, path); }
  boolAt(path) { return this.host.boolAt(this, path); }
  stringAt(path) { return this.host.stringAt(this, path); }
  typeAt(path) { return this.host.typeAt(this, path); }
}
