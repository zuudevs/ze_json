// ============================================================================
//  example.mjs — DEMO PEMAKAIAN PUBLIC API ze_json (js/ze_json.mjs)
// ============================================================================
//  Binding Raw WebAssembly (64-bit memory64, tanpa Emscripten). Modul public
//  API berada di js/ze_json.mjs; berkas ini hanya berisi demo, TANPA logika
//  wrapper — pipa pipa low-level tetap bisa diakses lewat `ze.ex` ABI dan
//  konstanta layout yang diekspor modul (lihat Demo 2 & 4).
//
//  Build target yang dibutuhkan:
//      cmake --preset wasm64-release
//      cmake --build build/wasm64-release
//    → build/wasm64-release/src/ze_json_core.wasm
//
//  Menjalankan (Node.js 24 sudah aktif memory64):
//      node examples/js/example.mjs
//  Jika runtime belum meng-enable memory64:
//      node --experimental-wasm-memory64 examples/js/example.mjs
// ============================================================================

import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { TextEncoder } from "node:util";

// PUBLIC API — wrapper + konstanta ABI + util low-level.
import {
  ZeJson,
  indexOfBytes,
  ERR_REF,
  KEY_BASE,
  KEY_CAP,
  PAYLOAD_BASE,
  TAPE_BASE,
  TAPE_ENTRIES,
} from "../../js/ze_json.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, "../..");
const encoder = new TextEncoder();

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
        // Nilai null menempati 1 tape entry (literal 'n'); harus
        // di-advance agar find_field() berikutnya tidak mem-parse
        // karakter 'n' tersisa sebagai pembuka string.
        value = doc.consumeNull();
        break;
      default:
        value = `<${hit.type}>`;
    }
    console.log(`  ${key.padEnd(9)}: ${hit.type.padEnd(8)} ${value}`);
  }

  // Traversal array: arraySize() sekali, lalu loop arrayNext() — dan
  // setelah habis, kursor kembali ke irama root object.
  const colors = doc.find("colors");
  if (colors.found && colors.type === "Array") {
    const size = doc.arraySize();
    console.log(`  colors   : Array, ${size} elemen:`);
    for (let i = 0; i < size; i++) {
      if (doc.arrayNext()) {
        const v = doc.typeOf() === "String"
          ? JSON.stringify(doc.asString())
          : `<${doc.typeOf()}>`;
        console.log(`    [${i}] ${v}`);
      }
    }
    // DRAIN: array_next sekali lagi hingga 0 — langkah melewatkan kursor
    // keluar ']' kembali ke irama root object.
    doc.arrayNext();
  }

  // Lanjutkan root object setelah array habis (bukti kursor rapi kembali)
  const position = doc.find("position");
  if (position.found) {
    console.log(
      `  position : ${position.type}` +
      ` (isi dalamnya dibaca via path query di bawah)`);
  }

  // QUERY PATH: satu panggilan wasm per kueri; kursor di-rewind dan
  // di-telusuri ulang di C++ (O(panjang path)), rerata nanosekon.
  // Bahkan kursor "habis" (di '}' akhir) pun tak masalah: find_path
  // mengulang dari root.
  console.log("  — path queries:");
  const px = doc.intAt("position.x");
  const py = doc.intAt("position.y");
  const c0 = doc.stringAt("colors[0]");
  const c1 = doc.stringAt("colors[1]");
  console.log(
    `  position.x/y : ${px.value}/${py.value} [${px.status}, ${py.status}]`);
  console.log(
    `  colors[0/1]  : ${JSON.stringify(c0.value)}/${JSON.stringify(c1.value)}` +
    ` [${c0.status}]`);
  const miss = doc.intAt("colors[9]");
  console.log(`  colors[9]    : status=${miss.status} (bagian dari ABI)`);

  console.log(`  arena terpakai: ${ze.arenaUsed()} byte`);
}

// ---------------------------------------------------------------------------
//  DEMO 2: hot loop — patch byte payload di-tempat, tanpa alokasi buffer JS.
//  Demo ini sengaja memakai ABI low-level (ze.ex.*) untuk menunjukkan pola
//  hidden high performance; konstanta layout diimpor dari public API.
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
    // (a) PATCH slot "id" langsung di memory (digit right-aligned)
    const digits = String(i).padStart(9, " ");
    for (let k = 0; k < digits.length; k++) {
      u8[baseNum + idSlotPos + k] = digits.charCodeAt(k);
    }

    // (b) reset + scan + bangun Session untuk dokumen (panjang tetap)
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
    // (d) reset dilakukan di awal iterasi berikutnya
  }

  const dtMs = Number(process.hrtime.bigint() - t0) / 1e6;
  console.log(`  dokumen     : ${N.toLocaleString("id-ID")}`);
  console.log(`  total       : ${dtMs.toFixed(1)} ms`);
  console.log(`  per dokumen : ${(dtMs / N * 1000).toFixed(2)} µs`);
  console.log(`  throughput  : ${Math.round(N / (dtMs / 1000)).toLocaleString("id-ID")} dok/dtk`);
  console.log(`  checksum    : ${checksum}`);
}

// ---------------------------------------------------------------------------
//  DEMO 4: A/B strategi baca dokumen yang sama:
//  A) per-langkah streaming: find_field kumulatif + getter (6 panggilan/dok)
//  B) path query: get_*_at (3 panggilan/dok, tiap kueri rewind dari root)
//  Kunci SEKALI ditulis; patch payload sama — membandingkan biaya scan
//  tape vs jumlah panggilan lintas JS↔WASM.
// ---------------------------------------------------------------------------

function demoPathVsSteps(ze) {
  console.log("\n[4] A/B strategi baca: per-langkah (6×) vs path (3×) per dokumen");

  const template =
    '{"id":         ,"name":"ze","score":3.1415926535,"active":true,"note":"ze"}';
  const tb = encoder.encode(template);
  const slot = indexOfBytes(tb, 0, '"id":') + 5;
  const base = Number(PAYLOAD_BASE);
  const payloadSize = BigInt(tb.length);
  const N = 100_000;

  // Template + ketiga kunci ditulis SEKALI — kedua strategi memakai region
  // kunci yang sama sehingga yang diukur hanya biaya panggilan wasm.
  ze.u8().set(tb, base);
  const key_at = (txt, ptr) => {
    const bytes = encoder.encode(txt);
    for (let k = 0; k < bytes.length; k++) {
      ze.u8()[Number(ptr) + k] = bytes[k];
    }
    return [ptr, BigInt(bytes.length)];
  };
  const keyId = key_at("id", KEY_BASE);
  const keyScore = key_at("score", KEY_BASE + 8n);
  const keyActive = key_at("active", KEY_BASE + 16n);

  const patched = (u8, digits) => {
    for (let k = 0; k < 9; k++) {
      u8[base + slot + k] = digits.charCodeAt(k);
    }
  };

  // --- Strategi A: find_field + getter (= 6 panggilan wasm/dokumen) ------
  const u8A = ze.u8();
  let sumA = 0n;
  let t0 = process.hrtime.bigint();
  for (let i = 0; i < N; i++) {
    patched(u8A, String(i).padStart(9, " "));
    ze.resetArena();
    const s = ze.ex.create_session(
      PAYLOAD_BASE, payloadSize, TAPE_BASE, TAPE_ENTRIES, ze.arena, ERR_REF);

    if (ze.ex.find_field(s, ...keyId) === 1) {
      sumA ^= ze.ex.get_int(s);
    }
    if (ze.ex.find_field(s, ...keyScore) === 1) {
      sumA += BigInt(Math.round(ze.ex.get_double(s) * 1e6));
    }
    if (ze.ex.find_field(s, ...keyActive) === 1) {
      sumA += BigInt(ze.ex.get_bool(s));
    }
  }
  const msA = Number(process.hrtime.bigint() - t0) / 1e6;

  // --- Strategi B: get_*_at path (= 3 panggilan wasm/dokumen) ------------
  // Satu Session cukup untuk SEMUA dokumen: tiap find_path me-rewind
  // kursor sendiri lalu menelusuri ulang dari root di C++.
  const u8B = ze.u8();
  let sumB = 0n;
  t0 = process.hrtime.bigint();
  const s = ze.ex.create_session(
    PAYLOAD_BASE, payloadSize, TAPE_BASE, TAPE_ENTRIES, ze.arena, ERR_REF);
  for (let i = 0; i < N; i++) {
    patched(u8B, String(i).padStart(9, " "));
    sumB ^= ze.ex.get_int_at(s, ...keyId, ERR_REF);
    sumB += BigInt(
      Math.round(ze.ex.get_double_at(s, ...keyScore, ERR_REF) * 1e6));
    sumB += BigInt(ze.ex.get_bool_at(s, ...keyActive, ERR_REF));
  }
  const msB = Number(process.hrtime.bigint() - t0) / 1e6;

  console.log(`  A per-langkah : ${msA.toFixed(1)} ms (${(msA / N * 1000).toFixed(2)} µs/dok)`);
  console.log(`  B path        : ${msB.toFixed(1)} ms (${(msB / N * 1000).toFixed(2)} µs/dok)`);
  console.log(
    `  checksum sama : ${sumA === sumB ? "ya" : "TIDAK"}`);
  // Interpretasi JUJUR: untuk kunci TOP-LEVEL yang berurutan di tape,
  // find_field kumulatif (A) menang — kursor maju sekali jalan, sementara
  // tiap kueri path me-rewind lalu menelusuri ulang dari root. Keunggulan
  // PATH ada pada akses ACak/bersarang ("items[0].name") yang via
  // per-langkah butuh orkestrasi JS yang tumbuh dengan kedalaman, sedangkan
  // path tetap SATU panggilan tidak peduli seberapa dalam targetnya.
  console.log(
    "  → kunci berurutan: per-langkah menang (scan 1× penuh vs 3× rewind);");
  console.log(
    "    path unggul untuk akses acak/bersarang (1 panggilan, kedalaman apa pun)");
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

const ze = await ZeJson.load();
demoTiny(ze);
demoHotLoop(ze);
demoPathVsSteps(ze);
demoBroken(ze);
console.log("\nSelesai — seluruh memori tetap, alokasi hot loop nol ✓");
