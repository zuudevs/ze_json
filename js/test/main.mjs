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

ze.close();
console.log(`\n${passed} lolos, ${failed} gagal · STATUS✓ ${STATUS.length} kode`);
process.exitCode = failed ? 1 : 0;
