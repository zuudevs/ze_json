# Benchmark JS: JSON.parse (V8) vs ze_json (wasm64) — rev 4aead05

Node v24.19.0, win32, 2026-10-22T15:25:31.043Z (benchmark.js v2, minTime 5s)

| dataset | JSON.parse | ze scan | ze scan+find | ze/JSON.parse |
|---|---|---|---|---|
| tiny (163 B) | 1.12 Mops/s | 883.68 kops/s | 492.15 kops/s | ×0.44 |
| mesh.json (335 KB) | 643 ops/s | 1.62 kops/s | 1.64 kops/s | ×2.56 |
| twitter.json (632 KB) | 599 ops/s | 400 ops/s | 378 ops/s | ×0.63 |

**Cara membaca:** ratio ×N = ze_json (scan+find) N kali lebih CEPAT daripada JSON.parse (N lebih besar = lebih baik). Ambil sebaik-baiknya dengan mempertimbangkan catatan jujur di header berkas.
