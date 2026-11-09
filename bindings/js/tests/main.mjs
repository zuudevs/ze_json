/**
 * main.mjs — smoke test for the ze_json wasm64 C-ABI binding (TASKS P3-2).
 *
 * Minimal, consistent: ABI version, basic scan cases, and scanner8 ==
 * reference_scan agreement inside the wasm module. Exit code 0 = pass.
 *
 * @cite 10.48550_arXiv.1902.08318 simdjson staging contract
 * @cite 10.14778_3115404.3115416 Mison structural indexing
 */
import { ZeJson, ZeDocument, ABI_VERSION, SCANNER_MODES } from '../ze_json.mjs';

let failures = 0;

function check(label, condition, detail = '') {
  if (condition) {
    console.log(`  ok   ${label}`);
  } else {
    failures += 1;
    console.error(`  FAIL ${label}${detail ? ` — ${detail}` : ''}`);
  }
}

const binding = ZeJson.load();
console.log(`[smoke] abi_version=${binding.abiVersion()} (expected ${ABI_VERSION})`);
check('abi version', binding.abiVersion() === ABI_VERSION);

// --- basic structural scan cases -------------------------------------------
const cases = [
  { input: '{"a":1}', expect: { ok: true, indices: [0, 4, 6] } },
  { input: '[1,[2],[3]]', expect: { ok: true, indices: [0, 2, 3, 5, 6, 7, 9, 10] } },
  { input: '"just a string"', expect: { ok: true, indices: [] } },
  { input: '{"k":"a[b],{c}"}', expect: { ok: true, indices: [0, 4, 15] } },
  { input: '"escaped\\"quote"', expect: { ok: true, indices: [] } },
  { input: '', expect: { ok: false, error: 'EmptyJson' } },
  { input: '{"unclosed', expect: { ok: false, error: 'InvalidJson' } },
  { input: '"dangling\\', expect: { ok: false, error: 'InvalidJson' } },
];

for (const { input, expect } of cases) {
  const doc = new ZeDocument(binding, Buffer.from(input, 'utf8'));
  const fast = doc.scan8();
  const reference = doc.referenceScan();
  const label = JSON.stringify(input.length > 24 ? `${input.slice(0, 21)}...` : input);

  check(`scan8 ${label} ok=${expect.ok}`, fast.ok === expect.ok);
  if (expect.ok) {
    check(`scan8 ${label} indices`, JSON.stringify(doc.indices(fast)) === JSON.stringify(expect.indices),
      `got [${doc.indices(fast)}] want [${expect.indices}]`);
  } else {
    check(`scan8 ${label} error`, fast.error === expect.error, `got ${fast.error}`);
  }
  // In-module agreement: fast path must equal the scalar reference.
  check(`reference_scan agreement ${label}`,
    fast.ok === reference.ok && fast.count === reference.count,
    `fast=${JSON.stringify(fast)} ref=${JSON.stringify(reference)}`);
}

// --- scanner-mode surface (R2) ----------------------------------------------
check('default scanner is structural', binding.defaultScanner === 'structural');
for (const mode of Object.keys(SCANNER_MODES)) {
  check(`mode '${mode}' available in wasm build`, binding.hasScanner(mode));
}
check('unknown mode rejected (TypeError)', (() => {
  try {
    binding.scan('turbo', 0, 0, 0, 0);
    return false;
  } catch (err) {
    return err instanceof TypeError;
  }
})());

const modeDoc = new ZeDocument(binding, Buffer.from('{"k":"a[b],{c}"}'), {
  scanner: 'scanner128',
});
const modeExpect = [0, 4, 15];
for (const mode of ['structural', 'scanner8', 'scanner64', 'scanner128', 'reference']) {
  const res = modeDoc.scan(mode);
  check(`modeDoc.scan('${mode}') indices`,
    res.ok && JSON.stringify(modeDoc.indices(res)) === JSON.stringify(modeExpect),
    `got [${modeDoc.indices(res)}]`);
}
// Preference: constructor option, override per call.
check('modeDoc preference honored', modeDoc.scanner === 'scanner128'
  && JSON.stringify(modeDoc.indices(modeDoc.scan())) === JSON.stringify(modeExpect));

// --- allocator sanity -------------------------------------------------------
const ptrA = binding.alloc(64);
const ptrB = binding.alloc(64);
check('bump allocator advances (16-aligned)', ptrB >= ptrA + 64 && ptrB % 16 === 0,
  `a=${ptrA} b=${ptrB}`);
binding.resetHeap();
const ptrReset = binding.alloc(64);
check('resetHeap rewinds to base region', ptrReset <= ptrA, `reset=${ptrReset} first=${ptrA}`);

console.log(failures === 0 ? '[smoke] PASS' : `[smoke] FAIL (${failures} failure(s))`);
process.exit(failures === 0 ? 0 : 1);
