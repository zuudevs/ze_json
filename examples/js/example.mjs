// ============================================================================
//  example.mjs — CONTOH PEMAKAIAN ze_json_core.wasm DARI JAVASCRIPT
// ============================================================================
//  Build target yang dibutuhkan:
//      cmake --preset wasm64-release
//      cmake --build build/wasm64-release
//    → build/wasm64-release/src/ze_json_core.wasm
//
//  Konsekuensi ABI wasm64: semua pointer & size dari JS berupa BigInt.
//  Konsekuensi API minimal: 1 Session = 1 dokumen JSON ber-root OBJECT.
//
//  Pola high performance yang dipakai contoh ini:
//    - WebAssembly.Memory digrow SEKALI di awal; seluruh region tetap;
//    - hot loop TANPA alokasi buffer baru: byte payload hanya di-PATCH,
//      arena di-reset, lalu create_session dipanggil ulang.
//
//  Menjalankan (Node.js):
//      node examples/js/example.mjs
//  Jika runtime belum meng-enable memory64:
//      node --experimental-wasm-memory64 examples/js/example.mjs
// ============================================================================

import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, "../..");
const WASM = join(ROOT, "build/wasm64-release/src/ze_json_core.wasm");

// ---------------------------------------------------------------------------
//  KONSTANTA ABI — sinkron dengan include/ze_json/ze_json.hpp
// ---------------------------------------------------------------------------

const STATUS = [
  "OK", "EMPTY_JSON", "INVALID_JSON", "UNEXPECTED_QUOTE",
  "INVALID_QUOTE", "INSUFFICIENT_BUFFER", "INVALID_UTF8",
];

const VALUE_TYPE = ["Array", "Object", "String", "Number", "Boolean", "Null"];

// ---------------------------------------------------------------------------
//  LAYOUT MEMORY (tetap selamanya):
//    [0x0000] scratch : slot out-param ABI
//                       0x00 int32 error │ 0x08 u64 data │ 0x10 u64 len
//    [0x0020] keys    : nama field untuk find_field (maks 512 byte)
//    [0x0400] payload : byte JSON (kapasitas 1 MiB)
//    [0x100400] tape  : posisi struktural (1 MiB = 131072 entri)
//    [0x200400] arena : Session + string unescape (1 MiB)
// ---------------------------------------------------------------------------

const PAGE_SIZE = 65536n; // 64 KiB — ukuran halaman WebAssembly.Memory

const ERR_REF = 0x00n; // int32 slot: kode status (out-param)
const DATA_REF = 0x08n; // u64 slot: pointer data string (out-param)
const LEN_REF = 0x10n; // u64 slot: panjang string (out-param)

const KEY_BASE = 0x0020n;
const KEY_CAP = 512n;

const PAYLOAD_BASE = 0x0400n;
const PAYLOAD_CAP = 1024n * 1024n;

const TAPE_BASE = PAYLOAD_BASE + PAYLOAD_CAP; // align-i64 aman
const TAPE_BYTES = PAYLOAD_CAP;
const TAPE_ENTRIES = PAYLOAD_CAP / 8n; // ABI minta SATUAN ENTRI, bukan byte!

const ARENA_BASE = TAPE_BASE + TAPE_BYTES;
const ARENA_CAP = 1024n * 1024n;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

// Pencarian byte manual — Uint8Array.prototype.indexOf tidak menerima string,
// jadi jangan memakai itu untuk mencari pola teks pada memory wasm.
function indexOfBytes(hay, start, text) {
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

class ZeJson {
  static async load(binaryPath = WASM) {
    let binary;
    try {
      binary = readFileSync(binaryPath);
    } catch {
      throw new Error(
        `${binaryPath} tidak ditemukan — build dulu:\n` +
          "    cmake --preset wasm64-release\n" +
          "    cmake --build build/wasm64-release",
      );
    }

    try {
      const { instance } = await WebAssembly.instantiate(binary, {});
      return new ZeJson(instance.exports);
    } catch (err) {
      // memory64 mungkin belum aktif pada runtime JS ini
      throw new Error(
        `gagal kompilasi wasm: ${err.message}\n` +
          "Coba: node --experimental-wasm-memory64 examples/js/example.mjs",
      );
    }
  }

  constructor(ex) {
    for (const name of [
      "create_arena", "reset_arena", "get_arena_used_space",
      "create_session", "find_field", "array_size", "array_next",
      "value_type", "get_int", "get_double", "get_bool", "get_string",
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

  // --- memori -------------------------------------------------------------

  buffer() {
    return this.ex.memory.buffer;
  }

  u8() {
    return new Uint8Array(this.buffer());
  }

  ensureCapacity(bytes) {
    const cur = BigInt(this.buffer().byteLength);
    if (bytes <= cur) return;
    const pages = (bytes - cur + PAGE_SIZE - 1n) / PAGE_SIZE;
    this.ex.memory.grow(pages); // memory64: grow(BigInt halaman)
  }

  // --- tulis region -----------------------------------------------------

  // Tulis byte mentah; mengembalikan (ptr, size) BigInt.
  // Satu byte 0 disisipkan SETELAH payload sebagai pengaman agar parser
  // angka tidak membaca melewati batas region ketika teks berhajar.
  writeRegion(base, cap, bytes) {
    if (BigInt(bytes.length) + 1n > cap) {
      throw new Error(`region terlalu kecil: butuh ${bytes.length + 1} byte`);
    }
    const baseNum = Number(base);
    const u8 = this.u8();
    u8.set(bytes, baseNum);
    u8[baseNum + bytes.length] = 0;
    return [base, BigInt(bytes.length)];
  }

  writeText(base, cap, text) {
    return this.writeRegion(base, cap, encoder.encode(text));
  }

  // --- lapisan API ------------------------------------------------------

  // Scan + bangun Session untuk satu dokumen.
  // TIDAK meng-grow memori; region payload/tape/arena dipakai ulang.
  openDocument(jsonText) {
    const [src, srcSize] = this.writeText(PAYLOAD_BASE, PAYLOAD_CAP, jsonText);
    return this.#openRaw(src, srcSize);
  }

  resetArena() {
    this.ex.reset_arena(this.arena);
  }

  arenaUsed() {
    return this.ex.get_arena_used_space(this.arena); // BigInt (i64)
  }

  #openRaw(src, srcSize) {
    this.resetArena(); // buang Session & string dokumen sebelumnya

    const session = this.ex.create_session(
      src, srcSize, TAPE_BASE, TAPE_ENTRIES, this.arena, ERR_REF);

    if (session === 0n) {
      const code = new DataView(this.buffer()).getInt32(Number(ERR_REF), true);
      throw new Error(`scan gagal: STATUS=${STATUS[code] ?? code}`);
    }
    return new Doc(this, session);
  }

  find(doc, key) {
    const [keyPtr, keySize] = this.writeText(KEY_BASE, KEY_CAP, key);
    // find_field mengembalikan i32 → Number (0/1), BUKAN BigInt
    return this.ex.find_field(doc.session, keyPtr, keySize) === 1;
  }

  typeOf(doc) {
    return VALUE_TYPE[Number(this.ex.value_type(doc.session))] ?? "?";
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
  //  Traversal array: array_size (sekali, saat kursor di '[') lalu
  //  loop array_next — elemen yang tidak dibaca di-skip otomatis.
  // ------------------------------------------------------------------
  arraySize(doc) {
    return Number(this.ex.array_size(doc.session)); // size_t → BigInt
  }

  arrayNext(doc) {
    // array_next mengembalikan i32 → Number (0/1), bukan BigInt
    return this.ex.array_next(doc.session) === 1;
  }

  // String ZERO-COPY: data menunjuk langsung ke payload / arena.
  // JS WAJIB menyalin (decode) SEBELUM resetArena() dipanggil lagi.
  stringOf(doc) {
    this.ex.get_string(doc.session, DATA_REF, LEN_REF);
    const view = new DataView(this.buffer());
    const data = view.getBigUint64(Number(DATA_REF), true);
    const len = view.getBigUint64(Number(LEN_REF), true);
    if (data === 0n) return "";
    return decoder.decode(
      new Uint8Array(this.buffer(), Number(data), Number(len)));
  }
}

class Doc {
  constructor(host, session) {
    this.host = host;
    this.session = session; // pointer C-ABI (BigInt)
  }

  // Cari + laporkan: { found, type }
  find(key) {
    if (!this.host.find(this, key)) return { found: false };
    return { found: true, type: this.host.typeOf(this) };
  }

  asString() { return this.host.stringOf(this); }
  asInt() { return this.host.intOf(this); }
  asDouble() { return this.host.doubleOf(this); }
  asBoolean() { return this.host.boolOf(this); }
}

// ---------------------------------------------------------------------------
//  DEMO 1: semua tipe value dari dataset/tiny.json
// ---------------------------------------------------------------------------

function demoTiny(ze) {
  const tiny = readFileSync(join(ROOT, "dataset/tiny.json"), "utf8");

  console.log("[1] openDocument(dataset/tiny.json)");
  const doc = ze.openDocument(tiny);

  for (const key of ["username", "age", "active", "status"]) {
    const hit = doc.find(key);
    if (!hit.found) {
      console.log(`  ${key.padEnd(9)}: NOT FOUND`);
      continue;
    }

    let value;
    switch (hit.type) {
      case "String":  value = JSON.stringify(doc.asString()); break;
      case "Number":  value = `${doc.asInt()}`; break;
      case "Boolean": value = `${doc.asBoolean()}`; break;
      case "Null":
        // Konsumsi kursor: nilai null menempati 1 tape entry ('n'); harus
        // di-advance sebelum find_field() berikutnya, jika tidak kursor
        // akan menganggap karakter 'n' itu sebagai pembuka string.
        ze.ex.get_bool(doc.session);
        value = null;
        break;
      default:
        value = `<${hit.type}>`;
    }
    console.log(`  ${key.padEnd(9)}: ${hit.type.padEnd(8)} ${value}`);
  }

  // SEBELUMNYA: nilai array tidak bisa di-consume sehingga find_field
  // berikutnya mati. Kini traversal array tersedia lewat array_size +
  // array_next — dan setelah habis, kursor kembali ke irama root object.
  const colors = doc.find("colors");
  if (colors.found && colors.type === "Array") {
    const size = ze.arraySize(doc);
    console.log(`  colors   : Array, ${size} elemen:`);
    for (let i = 0; i < size; i++) {
      if (ze.arrayNext(doc)) {
        const v = ze.typeOf(doc) === "String"
          ? JSON.stringify(doc.asString())
          : `<${ze.typeOf(doc)}>`;
        console.log(`    [${i}] ${v}`);
      }
    }
    // DRAIN: panggil array_next sekali lagi hingga 0 — langkah ini yang
    // melewatkan kursor keluar ']' kembali ke irama root object.
    ze.arrayNext(doc);
  }

  // Lanjutkan root object setelah array habis (bukti kursor rapi kembali)
  const position = doc.find("position");
  if (position.found) {
    console.log(
      `  position : ${position.type}` +
      ` (nested object — traversal dalam belum tersedia)`);
  }

  console.log(`  arena terpakai: ${ze.arenaUsed()} byte`);
}

// ---------------------------------------------------------------------------
//  DEMO 2: hot loop — patch byte payload di-tempat, tanpa alokasi buffer JS
// ---------------------------------------------------------------------------

function demoHotLoop(ze) {
  console.log("\n[2] Hot loop 100.000 dokumen (patch-in-place, arena di-reset)");

  // Template dengan field "id" ber-lebar TETAP (9 slot). Slot di-PATCH per
  // dokumen dengan digit right-aligned dan spasi (whitespace diabaikan
  // scanner & number parser):
  //   {"id":        0,...}   →   {"id":       42,...}   dst.
  const template =
    '{"id":         ,"name":"ze","score":3.1415926535,"active":true,"note":"ze"}';
  const templateBytes = encoder.encode(template);
  const idSlotPos =
    indexOfBytes(templateBytes, 0, '"id":') + 5; // posisi sesudah ':'

  // Tulis template SEKALI ke region payload — semua dokumen mem-PATCH slot
  // ini saja; panjang payload selalu konstan.
  const baseNum = Number(PAYLOAD_BASE);
  ze.u8().set(templateBytes, baseNum);
  const payloadSize = BigInt(templateBytes.length);

  const N = 100_000;
  let checksum = 0n;

  const u8 = ze.u8();

  const t0 = process.hrtime.bigint();

  for (let i = 0; i < N; i++) {
    // (a) PATCH slot "id" langsung di memory (digit right-aligned, spasi
    //     pada sisa slot tidak perlu di-tulis ulang — spasi dari template
    //     tetap ditoleransi oleh parser)
    const digits = String(i).padStart(9, " ");
    for (let k = 0; k < digits.length; k++) {
      u8[baseNum + idSlotPos + k] = digits.charCodeAt(k);
    }

    // (b) reset + scan + bangun Session untuk dokumen (panjang payload tetap
    //     konstan)
    ze.resetArena();
    const session = ze.ex.create_session(
      PAYLOAD_BASE, payloadSize, TAPE_BASE, TAPE_ENTRIES, ze.arena, ERR_REF);
    if (session === 0n) throw new Error(`scan gagal di dokumen ke-${i}`);

    // (c) baca field on-demand (hanya yang dibutuhkan)
    const [idPtr, idLen] = ze.writeText(KEY_BASE, KEY_CAP, "id");
    if (ze.ex.find_field(session, idPtr, idLen) === 1) {
      checksum ^= ze.ex.get_int(session); // i64 → BigInt
    }

    const [sPtr, sLen] = ze.writeText(KEY_BASE, KEY_CAP, "score");
    if (ze.ex.find_field(session, sPtr, sLen) === 1) {
      checksum += BigInt(Math.round(ze.ex.get_double(session) * 1e6));
    }

    const [aPtr, aLen] = ze.writeText(KEY_BASE, KEY_CAP, "active");
    if (ze.ex.find_field(session, aPtr, aLen) === 1) {
      if (ze.ex.get_bool(session) !== 1) checksum += 1n;
    }
    // (d) reset dilakukan di awal iterasi berikutnya (opsi destroy_session)
  }

  const dtMs = Number(process.hrtime.bigint() - t0) / 1e6;
  console.log(`  dokumen     : ${N.toLocaleString("id-ID")}`);
  console.log(`  total       : ${dtMs.toFixed(1)} ms`);
  console.log(`  per dokumen : ${(dtMs / N).toFixed(4)} ms`);
  console.log(`  throughput  : ${Math.round(N / (dtMs / 1000)).toLocaleString("id-ID")} dok/dtk`);
  console.log(`  checksum    : ${checksum}`);
}

// ---------------------------------------------------------------------------
//  DEMO 3: dokumen rusak → kode STATUS ABI
// ---------------------------------------------------------------------------

function demoBroken(ze) {
  console.log("\n[3] Dokumen rusak (string tak ditutup)");
  try {
    ze.openDocument('{"broken": "unterminated');
  } catch (e) {
    console.log(`  → ${e.message}`);
  }
}

// ---------------------------------------------------------------------------

console.log("Muat wasm:", WASM);
const ze = await ZeJson.load();
demoTiny(ze);
demoHotLoop(ze);
demoBroken(ze);
console.log("\nSelesai — seluruh memori tetap, alokasi hot loop nol ✓");
