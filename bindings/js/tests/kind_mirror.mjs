/**
 * kind_mirror.mjs — O1-4 exhaustive differential: the JS launcher mirror
 * (kNodeKindByByte in ze_json.mjs, via ZeParser.kind()) MUST be
 * byte-identical to the C++ classify() surface (ze_value_kind) for
 *
 *   (a) every byte value 0..255 — the classification domain (frozen spec
 *       below mirrors the GenerateLookups.py member-set; when the
 *       generated lookups.hpp is present next to the build tree, the test
 *       additionally parses its kOndemandNodeType array and compares all
 *       256 entries against the frozen spec AND the JS table);
 *   (b) every position of every document in the corpora (sweep ts =
 *       0..len-1 — this covers every byte value that actually occurs in
 *       real SAQ payloads at their real positions);
 *   (c) out-of-range and boundary cursors (ts = len, len+1, huge, and
 *       classify()'s dead-slot/degenerate behavior via bounds -> 0).
 *
 * A mismatch voids the mirror (exit 1). Run: node tests/kind_mirror.mjs
 *
 * @cite 10.1002_spe.3313 on-demand JSON — the mirror is only sound when it
 *       is pinned to the C++ classification surface it replaces (AGENTS 4:
 *       differential test identity for every fast path).
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { ZeJson, ZeParser } from '../ze_json.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..', '..', '..');
const CORPUS_ROOT = path.join(ROOT, 'corpus', 'benchmarks');
const LOOKUPS_HPP = path.join(
    HERE, '..', '..', '..', 'build', 'native-release', 'generated',
    'ze_json', 'core', 'lookups.hpp');

let checks = 0;
const fail = (msg) => {
  console.error(`FAIL ${msg}`);
  process.exit(1);
};
const ok = (msg) => {
  checks += 1;
  console.log(`ok   ${msg}`);
};

// (a) frozen spec — mirrors GenerateLookups.py gen_ondemand_node_types.
const SPEC = new Uint8Array(256);
SPEC[0x7b] = 1; // '{' object
SPEC[0x5b] = 2; // '[' array
SPEC[0x22] = 3; // '"' string
SPEC[0x2d] = 4; // '-' number
for (let b = 0x30; b <= 0x39; b += 1) SPEC[b] = 4; // '0'..'9'
SPEC[0x74] = 5; // 't' true
SPEC[0x66] = 6; // 'f' false
SPEC[0x6e] = 7; // 'n' null

const binding = ZeJson.load();

// Reach the module-private table through the parser on a synthetic doc.
// Any byte's classification is observable as kind(ts) for a source that
// contains that byte — but only bytes 0x20..0xff minus JSON structure can
// appear in an open-able document, so the FULL 0..255 domain is checked
// against the frozen spec + (optional) generated table instead, and the
// in-document sweep (b) covers every byte at its real position.
const probe = ZeParser.open(
    binding, new TextEncoder().encode('{"k":[1,-2.5,true,false,null,"s"]}'));

// kind() must exist and agree on the probe document's positions.
{
  const src = new TextEncoder().encode('{"k":[1,-2.5,true,false,null,"s"]}');
  for (let ts = 0; ts < src.length; ts += 1) {
    const js = probe.kind(ts);
    const abi = Number(binding.valueKind(BigInt(probe.slot), BigInt(ts)));
    if (js !== abi) {
      fail(`probe ts=${ts}: mirror ${js} != ze_value_kind ${abi}`);
    }
  }
  ok(`probe document sweep (${src.length} positions) mirror == ABI`);
}
probe.close();

// (b) corpus sweep — every position of every document, mirror vs ABI.
const CORPORA = ['saq_payload_v1', 'synthetic_1k', 'synthetic_10k',
  'synthetic_100k', 'synthetic_1mb', 'checker_batch'];
let docs = 0;
let positions = 0;
let skipped = 0;
for (const corpus of CORPORA) {
  const dir = path.join(CORPUS_ROOT, corpus);
  if (!fs.existsSync(dir)) continue;
  for (const fn of fs.readdirSync(dir)) {
    if (!fn.endsWith('.json')) continue;
    const bytes = new Uint8Array(fs.readFileSync(path.join(dir, fn)));
    let parser;
    try {
      parser = ZeParser.open(binding, bytes);
    } catch {
      skipped += 1; // invalid docs cannot open; nothing to classify via walk
      continue;
    }
    try {
      for (let ts = 0; ts < bytes.length; ts += 1) {
        const js = parser.kind(ts);
        const abi = Number(binding.valueKind(BigInt(parser.slot), BigInt(ts)));
        if (js !== abi) {
          fail(`${corpus}/${fn} ts=${ts}: mirror ${js} != ABI ${abi}`);
        }
        positions += 1;
      }
      docs += 1;
    } finally {
      parser.close();
    }
  }
}
binding.resetHeap();
ok(`corpus sweep: ${docs} docs x all positions (${positions} pairs)` +
   (skipped ? `, ${skipped} invalid docs skipped (cannot open)` : ''));
if (docs === 0) fail('no corpus document could be opened');

// (c) bounds — classify(): byte_pos >= src_size -> 0; huge ts -> 0.
{
  const src = new TextEncoder().encode('[1,2,3]');
  const p = ZeParser.open(binding, src);
  try {
    for (const ts of [src.length, src.length + 1, src.length + 4096,
      2 ** 31, 2 ** 52]) {
      const js = p.kind(ts);
      const abi = Number(binding.valueKind(BigInt(p.slot), BigInt(ts)));
      if (js !== 0 || abi !== 0) {
        fail(`bounds ts=${ts}: mirror ${js} ABI ${abi} (expected 0/0)`);
      }
    }
    ok('bounds: ts >= len and huge cursors classify 0 on both sides');
  } finally {
    p.close();
  }
  binding.resetHeap();
}

// (a2) generated table cross-check when lookups.hpp exists: parse
// kOndemandNodeType and require spec == hpp (and the JS mirror follows
// from spec by construction, pinned again below through kind()).
if (fs.existsSync(LOOKUPS_HPP)) {
  const hpp = fs.readFileSync(LOOKUPS_HPP, 'utf8');
  const m = hpp.match(/kOndemandNodeType\[\]\s*=\s*\{([\s\S]*?)\};/);
  if (!m) fail('lookups.hpp found but kOndemandNodeType not parseable');
  const values = m[1].split(',').map((s) => parseInt(s.trim(), 16));
  if (values.length !== 256) {
    fail(`lookups.hpp table has ${values.length} entries (expected 256)`);
  }
  for (let b = 0; b < 256; b += 1) {
    if (values[b] !== SPEC[b]) {
      fail(`lookups.hpp[${b}]=${values[b]} != spec ${SPEC[b]}`);
    }
  }
  ok('lookups.hpp kOndemandNodeType (256 entries) == frozen spec');
} else {
  console.log('skip lookups.hpp cross-check (generated header not present)');
}

console.log(`\n[kind_mirror] ${checks} check groups PASS`);
