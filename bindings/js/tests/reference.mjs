/**
 * reference.mjs — plain JavaScript reference implementation of the scanner
 * contract (tests/differential.mjs uses it as the third leg of the gate).
 *
 * Contract (mirrors include/ze_json/ondemand/reference.hpp):
 *   - output = offsets of [ ] { } , : outside strings
 *   - '"' toggles string state; '\' inside a string escapes the next byte
 *   - errors: EmptyJson (len 0), InvalidJson (dangling escape / unclosed string)
 */

export function referenceScan(bytes) {
  const n = bytes.length;
  if (n === 0) {
    return { ok: false, error: 'EmptyJson', indices: [] };
  }
  const indices = [];
  let inString = false;
  for (let i = 0; i < n; i += 1) {
    const c = bytes[i];
    if (inString) {
      if (c === 0x5c) {
        i += 1;
        if (i >= n) {
          return { ok: false, error: 'InvalidJson', indices: [] };
        }
      } else if (c === 0x22) {
        inString = false;
      }
      continue;
    }
    if (c === 0x22) {
      inString = true;
    } else if (c === 0x5b || c === 0x5d || c === 0x7b || c === 0x7d || c === 0x2c || c === 0x3a) {
      indices.push(i);
    }
  }
  if (inString) {
    return { ok: false, error: 'InvalidJson', indices: [] };
  }
  return { ok: true, count: indices.length, indices };
}
