/**
 * main.mjs — smoke test for the ze_json wasm64 C-ABI binding (TASKS P3-2).
 *
 * Minimal, consistent: ABI version, basic scan cases, and scanner8 ==
 * reference_scan agreement inside the wasm module. Exit code 0 = pass.
 *
 * @cite 10.48550_arXiv.1902.08318 simdjson staging contract
 * @cite 10.14778_3115404.3115416 Mison structural indexing
 */
import { ZeJson, ZeDocument, ZeParser, ZeParserError, ABI_VERSION,
  SCANNER_MODES, PARSER_SLOT_MAX } from '../ze_json.mjs';

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

// --- on-demand parser surface (P7-5) ---------------------------------------
// parse() = full materialization; for plain cases the tree must equal
// JSON.parse (same engine, canonical stringify on both sides). The two
// documented divergence cases (-0 sign, lone-surrogate pass-through) get
// explicit checks instead of stringify — JSON.stringify loses the -0 sign
// and the escape-preserving wrapper semantics differs from a V8 surrogate.
const parseDoc = (text) => {
  const parser = ZeParser.open(binding, Buffer.from(text, 'utf8'));
  try {
    return parser.parse();
  } finally {
    parser.close();
  }
};

const parseParityCases = [
  'null', 'true', ' false ', '42', '-3', '-2.5e2', '9007199254740993',
  '1e-400', '"plain"', '"tab\\tnewline\\n"', '"\\u0000"', '"\\ud83d\\ude00"',
  '[]', '[1,2,3]', '[0.1, [true, null]]', '{}', '{"a":1,"b":[1,{"c":"d"}]}',
  '{"k":"a[b],{c}"}', '{"a\\t":"b\\n\\u0041"}', '"ex\\"c"', '"end\\\\"',
  '"\\ufeffBOM-kept"', '{"\\ufeffk":"a\\ufeffb"}',
  '[[[[["deep"]]]]]', '[1e400]',
];
for (const text of parseParityCases) {
  let ok = false;
  let detail = '';
  try {
    const value = parseDoc(text);
    const v8 = JSON.parse(text);
    ok = JSON.stringify(value) === JSON.stringify(v8);
    detail = `ze=${JSON.stringify(value)} v8=${JSON.stringify(v8)}`;
  } catch (err) {
    if (err instanceof ZeParserError) {
      detail = `unexpected reject: ${err.message}`;
    } else {
      throw err;
    }
  }
  check(`parse ${JSON.stringify(text)} equals JSON.parse`, ok, detail);
}

// Documented divergences: exact f64/-0 identity and lone-surrogate literal.
check('parse "-0" keeps negative zero',
  Object.is(parseDoc('-0'), -0) && Object.is(JSON.parse('-0'), -0));
check('parse "1e400" overflows to Infinity',
  parseDoc('1e400') === Infinity && JSON.parse('1e400') === Infinity);
check('parse lone surrogate escape passes through verbatim',
  parseDoc('"\\ud800"') === '\\ud800');
check('parse decode target is UTF-8 of the astral pair',
  Buffer.from(parseDoc('"\\ud83d\\ude00"'), 'utf8')
    .equals(Buffer.from('\xF0\x9F\x98\x80', 'binary')));

// R5-3 (O1-2): the wrapper decodes wasm spans with a TextDecoder configured
// ignoreBOM:true (do NOT strip U+FEFF) — parity above covers the parser path;
// this guard pins the replacement-char policy against the V8 Buffer decoder
// on malformed bytes (unreachable from the C++ decode output, kept as a
// belt-and-suspenders check for any future direct caller).
{
  const dec = new TextDecoder('utf8', { ignoreBOM: true });
  const malformed = [[0xFF], [0xC0, 0x80], [0xED, 0xA0, 0x80],
    [0xF4, 0x90, 0x80, 0x80], [0xC2], [0xE2, 0x82]];
  const ok = malformed.every((b) => dec.decode(new Uint8Array(b))
    === Buffer.from(b).toString('utf8'));
  check('utf8 decoder replacement parity vs Buffer on malformed bytes', ok,
    `cases=${malformed.length}`);
}

// Own-key semantics: "__proto__" must stay an own key (JSON.parse parity).
const protoValue = parseDoc('{"__proto__": 1}');
check('parse "__proto__" key stays an own property',
  Object.prototype.hasOwnProperty.call(protoValue, '__proto__')
  && Object.getPrototypeOf(protoValue) === null
  && protoValue['__proto__'] === 1);

// Assignment-path semantics (#readObject): duplicates are last-wins at the
// first-occurrence position, "__proto__" stays an own key wherever it
// appears, and integer-index-like keys follow OrdinaryOwnPropertyKeys on
// both engines (all three are data-property semantics of a null-prototype
// receiver, so the fast assignment path must not diverge from V8).
const dupValue = parseDoc('{"a": 1, "b": 2, "a": 3}');
check('parse duplicate keys are last-wins at first position',
  dupValue.a === 3 && dupValue.b === 2
  && JSON.stringify(Object.keys(dupValue)) === JSON.stringify(['a', 'b']));
check('parse duplicate-key tree equals JSON.parse',
  JSON.stringify(dupValue)
  === JSON.stringify(JSON.parse('{"a": 1, "b": 2, "a": 3}')));

const protoMid = parseDoc('{"a": 1, "__proto__": {"nested": 2}, "c": null}');
check('parse mid-document "__proto__" stays an own key',
  Object.prototype.hasOwnProperty.call(protoMid, '__proto__')
  && Object.getPrototypeOf(protoMid) === null
  && protoMid['__proto__'].nested === 2
  && JSON.stringify(Object.keys(protoMid))
  === JSON.stringify(['a', '__proto__', 'c']));
check('parse mid-document "__proto__" tree equals JSON.parse',
  JSON.stringify(protoMid)
  === JSON.stringify(JSON.parse('{"a": 1, "__proto__": {"nested": 2}, "c": null}')));

const numKeyValue = parseDoc('{"a": 1, "123": 2, "3": 3, "z": 4}');
const v8NumKeyValue = JSON.parse('{"a": 1, "123": 2, "3": 3, "z": 4}');
check('parse integer-index-like key order equals JSON.parse',
  JSON.stringify(Object.keys(numKeyValue))
  === JSON.stringify(Object.keys(v8NumKeyValue))
  && JSON.stringify(numKeyValue) === JSON.stringify(v8NumKeyValue));

// Framing/grammar rejects — the walk must surface a ZeParserError verdict.
const parseRejectCases = [
  '', '  \n', 'tru', 'nul', '5 5', 'true x', '"a" 4', '1e3\r,', '[,]',
  '[1,]', '[1 2]', '["a" "b"]', '[1, 2', '[1:', '[1e]', '[01]',
  '{"a": 1,}', '{"a": }', '{"a"}', '{,}', '{"a":: 1}', '{1: 2}',
  '{"a": "b"} {"b": 2}', '[] []', '"just a string" trailing',
];
for (const text of parseRejectCases) {
  let threw = false;
  let detail = '';
  try {
    parseDoc(text);
  } catch (err) {
    threw = err instanceof ZeParserError;
    detail = err.message;
  }
  check(`parse rejects ${JSON.stringify(text)}`, threw, detail);
}

// Low-level parser surface: counts, kinds, frame walking, slot lifecycle.
{
  const parser = ZeParser.open(binding, Buffer.from('[1, 2, 3]', 'utf8'));
  check('root kind is object/array classified', parser.kind(parser.rootTs) === 2);
  check('countElements of the root array',
    parser.countElements(parser.rootTs) === 3);
  const frame = parser.containerBegin(parser.rootTs, 0);
  const kinds = [];
  let cursor = frame;
  for (let i = 0; i < 4; i += 1) {
    kinds.push(parser.kind(cursor.ts));
    cursor = parser.arrayNext(cursor.ts, cursor.cursor);
    if (cursor === null) {
      break;
    }
  }
  check('frame walk classifies [number, number, number] then done',
    JSON.stringify(kinds.slice(0, 3)) === JSON.stringify([4, 4, 4])
    && kinds[3] === undefined, `kinds=${JSON.stringify(kinds)}`);
  parser.close();
  check('close() is idempotent', (parser.close(), true));
}

check('two parsers live concurrently', (() => {
  const a = ZeParser.open(binding, Buffer.from('{"a":1}', 'utf8'));
  const b = ZeParser.open(binding, Buffer.from('[true, null, 2]', 'utf8'));
  const ok = JSON.stringify(a.parse()) === JSON.stringify(JSON.parse('{"a":1}'))
    && JSON.stringify(b.parse()) === JSON.stringify(JSON.parse('[true, null, 2]'));
  a.close();
  b.close();
  return ok;
})());

check('slot pool fills to kParserSlotMax then fails loud', (() => {
  const bytes = Buffer.from('1', 'utf8');
  for (let i = 0; i < PARSER_SLOT_MAX; i += 1) {
    const ptr = binding.alloc(1);
    binding.writeBytes(ptr, bytes);
    const open = binding.openParser(ptr, 1);
    if (!open.ok) {
      console.error(`  slot ${i} unexpectedly rejected: ${open.error}`);
      binding.resetHeap();
      return false;
    }
  }
  const overflowPtr = binding.alloc(1);
  const overflow = binding.openParser(overflowPtr, 1);
  binding.resetHeap(); // closes every open slot in one step
  return overflow.ok === false && overflow.error === 'InsufficientBuffer'
    && binding.openParser(overflowPtr, 1).ok === true;
})());

check('resetHeap closes live parser slots', (() => {
  const parser = ZeParser.open(binding, Buffer.from('"x"', 'utf8'));
  const live = binding.valueKind(parser.slot, parser.rootTs);
  binding.resetHeap();
  const dead = binding.valueKind(parser.slot, parser.rootTs);
  return live === 3 && dead === 0; // string when open, unknown when closed
})());

console.log(failures === 0 ? '[smoke] PASS' : `[smoke] FAIL (${failures} failure(s))`);
process.exit(failures === 0 ? 0 : 1);
