// ============================================================================
//  test/main.mjs — SMOKE TEST public API @zuudevs/ze-json
// ============================================================================
//  Uji end-to-end jalur JS ↔ wasm64: lintas tipe nilai, traversal array,
//  drain, konsumsi null, path query (benar/miss/type-mismatch), dan
//  penolakan dokumen rusak. Exit code non-zero bila ada y ang gagal.
//
//  Menjalankan:  node js/test/main.mjs   (atau: npm test --prefix js)
// ============================================================================

import { STATUS, ZeJson } from "../ze_json.mjs";

let passed = 0;
let failed = 0;

function check(label, cond) {
  if (cond) {
    passed++;
    console.log(`  ✓ ${label}`);
  } else {
    failed++;
    console.error(`  ✗ ${label}`);
  }
}

function throwsWithStatus(fn, expected) {
  try {
    fn();
  } catch (e) {
    return e.message.includes(expected);
  }
  return false;
}

const ze = await ZeJson.load();

// --- 1. Semua tipe skalar + null -------------------------------------------
{
  const doc = ze.openDocument(
    '{"id":21,"name":"ze","score":3.5,"active":true,"status":null}',
  );

  const id = doc.find("id");
  check("find id → Number", id.found && id.type === "Number");
  check("asInt → 21n", doc.asInt() === 21n);

  check("find name → String", doc.find("name").type === "String");
  check("asString → 'ze'", doc.asString() === "ze");

  check("find score → Number", doc.find("score").type === "Number");
  check("asDouble → 3.5", doc.asDouble() === 3.5);

  check("find active → Boolean", doc.find("active").type === "Boolean");
  check("asBoolean → true", doc.asBoolean() === true);

  check("find status → Null", doc.find("status").type === "Null");
  check("consumeNull → null", doc.consumeNull() === null);

  check("find missing → NOT_FOUND", doc.find("nope").found === false);

  // Dokumen baru memakai ulang region & arena yang sama.
  const doc2 = ze.openDocument('{"v":7}');
  check("openDocument berikutnya → reusable",
    doc2.find("v").found && doc2.asInt() === 7n);
}

// --- 1b. Getter menolak tipe salah (v0.1.2) ----------------------------------
{
  // Kursor habis (find gagal berjalan sampai akhir): dahulu asString
  // melempar RangeError kabur (span sampah dari get_string); kini
  // STATUS=TYPE_MISMATCH bersih. Dokumen terpisah — find() forward-only.
  const d1 = ze.openDocument('{"id":21,"name":"ze"}');
  d1.find("missing");
  check("asString pasca find gagal → TYPE_MISMATCH",
    throwsWithStatus(() => d1.asString(), "TYPE_MISMATCH"));

  // Tipe salah dengan kursor VALID — bergerak maju sesuai urutan dokumen.
  const doc = ze.openDocument(
    '{"id":21,"name":"ze","status":null,"ok":true}',
  );

  doc.find("id");
  check("asString pada Number → TYPE_MISMATCH",
    throwsWithStatus(() => doc.asString(), "TYPE_MISMATCH"));

  doc.find("name");
  check("asInt pada String → TYPE_MISMATCH",
    throwsWithStatus(() => doc.asInt(), "TYPE_MISMATCH"));

  doc.find("status");
  check("asDouble pada Null → TYPE_MISMATCH",
    throwsWithStatus(() => doc.asDouble(), "TYPE_MISMATCH"));
  doc.consumeNull();

  doc.find("ok");
  check("asBoolean pada Boolean tetap benar", doc.asBoolean() === true);

  // Nilai benar tetap terbaca lewat path query (rewind sendiri).
  check("stringAt(name) tetap 'ze'", doc.stringAt("name").value === "ze");
  check("intAt(id) tetap 21", doc.intAt("id").value === 21);
}

// --- 2. Traversal array + drain + drain-tengah terlindungi ------------------
{
  const doc = ze.openDocument(
    '{"colors":["white","black","red"],"tail":9}',
  );
  const hit = doc.find("colors");
  check("array_size → 3", hit.type === "Array" && doc.arraySize() === 3);

  const got = [];
  while (doc.arrayNext()) {
    if (doc.typeOf() === "String") got.push(doc.asString());
  }
  check("semua elemen terbaca", got.join(",") === "white,black,red");

  // array_next terakhir me-drain kursor keluar ']' → root object hidup lagi.
  check("find setelah drain → 'tail':9",
    doc.find("tail").found && doc.asInt() === 9n);
}

// --- 2b. Streaming array: TANPA arraySize (satu lintasan) -------------------
{
  const doc = ze.openDocument(
    '{"colors":["white","black","red"],"tail":9}',
  );
  const hit = doc.find("colors");
  check("streaming typeOf → Array", hit.type === "Array");

  const got = [];
  while (doc.arrayNext()) { // pola streaming: arraySize dilewati
    if (doc.typeOf() === "String") got.push(doc.asString());
  }
  check("streaming semua elemen terbaca",
    got.join(",") === "white,black,red");

  check("streaming: find setelah drain → 'tail':9",
    doc.find("tail").found && doc.asInt() === 9n);

  // Campuran pola pada DOKUMEN BARU: streaming "a" drain penuh, lalu field
  // lain diiterasi dengan kontrak counted (arraySize + arrayNext).
  const doc2 = ze.openDocument('{"a":[1,2,3],"b":["p","q"]}');
  doc2.find("a");
  let countA = 0;
  while (doc2.arrayNext()) countA++;
  check("streaming a → 3 elemen", countA === 3);

  doc2.find("b");
  check("counted arraySize(b) → 2", doc2.arraySize() === 2);
  const gotB = [];
  while (doc2.arrayNext()) {
    if (doc2.typeOf() === "String") gotB.push(doc2.asString());
  }
  check("counted b terbaca", gotB.join(",") === "p,q");
}

// --- 3. Path query: flat / nested / array / dalam / miss / type-mismatch ----
{
  const doc = ze.openDocument(
    '{"position":{"x":24,"y":48},"colors":["white","black"],"items":[{"n":1},{"n":3}]}',
  );

  check("intAt position.x → 24", doc.intAt("position.x").value === 24);
  check("doubleAt items[1].n → 3",
    doc.doubleAt("items[1].n").value === 3);
  check("stringAt colors[1] → 'black'",
    doc.stringAt("colors[1]").value === "black");
  check("typeAt position → Object",
    doc.typeAt("position").type === "Object");

  check("intAt colors[9] → NOT_FOUND",
    doc.intAt("colors[9]").status === "NOT_FOUND");
  check("intAt name → TYPE_MISMATCH",
    doc.intAt("colors[0]").status === "TYPE_MISMATCH");
  check("intAt leading-dot → INVALID_PATH",
    doc.intAt(".position").status === "INVALID_PATH");
  check("intAt '[' tanpa ']' → INVALID_PATH",
    doc.intAt("position.x[").status === "INVALID_PATH");
}

// --- 4. Dokumen rusak di-tolak dengan STATUS ABI ----------------------------
{
  check(
    "openDocument string tak ditutup → UNEXPECTED_QUOTE",
    throwsWithStatus(
      () => ze.openDocument('{"broken": "unterminated'),
      "UNEXPECTED_QUOTE",
    ),
  );
  check(
    "openDocument depth tak seimbang → INVALID_JSON",
    throwsWithStatus(() => ze.openDocument('{"a":'), "INVALID_JSON"),
  );
}

// --- 5. Regresi payload di-atas 64 KiB --------------------------------------
// Sebelum fix --global-base, penulisan payload > ~63,9 KB menyapu segmen
// .rodata wasm (kStructural/kUtf8Dfa) yang menyebabkan INVALID_UTF8 palsu.
{
  // Pad ASCII besar dengan karakter UTF-8 multi-byte di tengah string.
  const besar = "a".repeat(65_000) + "名名名" + "b".repeat(1_000);
  check(
    "payload ~66 KB ber-UTF-8 → scan OK",
    (() => {
      const doc = ze.openDocument(`{"s":"${besar}"}`);
      return doc.find("s").found;
    })(),
  );
  check(
    "payload string besar masih di-emit dengan benar",
    (() => {
      const doc = ze.openDocument(`{"s":"x${besar}"}`);
      doc.find("s");
      const v = doc.asString();
      // 65.000 'a' + 3 karakter UTF-8 + 1.000 'b' + karakter 'x' = 66.004 char
      return v.startsWith("x") && v.endsWith("b") && v.length === 66_004;
    })(),
  );
}

// --- 6. Varian scanner + version compatibility (v0.1.5) --------------------
{
  // Nomor versi tersedia (v0.1.5+).
  check("versionNumber tersedia (>= 2026000105)",
    ze.versionNumber !== null && ze.versionNumber >= 2026000105n);
  check("hasScannerSelection true (v0.1.5+)",
    ze.hasScannerSelection === true);

  // Varian tak valid → error sebelum wasm dimuat.
  check("scanner tidak valid → error",
    (() => {
      try { new ZeJson({}, "bogus"); return false; }
      catch { return true; }
    })());

  // NOTE: pengujian end-to-end varian scalar/wide dijalankan di NATIVE
  // session_test (51 test × 2 preset) — menghindari crash libuv
  // multiple-instances di Node.js. Binding JS menerima opsi dan
  // memanggil ABI yang benar; kontrak varian dijuni oleh native.
}

ze.close();
console.log(`\n${passed} lolos, ${failed} gagal · STATUS✓ ${STATUS.length} kode`);
process.exitCode = failed ? 1 : 0;
