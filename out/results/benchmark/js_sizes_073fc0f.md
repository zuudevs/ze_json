# Benchmark JS sapuan ukuran: JSON.parse vs ze_json — rev 073fc0f

Node v24.19.0, win32, 2026-10-24T02:08:01.267Z (benchmark.js v2)

Bentuk dokumen konstan; hanya lebar array items menskalakan. µs/op — lebih kecil = lebih cepat.

| ukuran | parse | ze scan | baca JSON | baca ze | iter JSON | iter ze |
|---|---|---|---|---|---|---|
| 95 B | 1.017 | 0.567 | 1.066 | 2.428 | 1.201 | 2.293 |
| 268 B | 3.354 | 1.063 | 2.58 | 2.91 | 3.201 | 9.173 |
| 1 KiB | 11.339 | 3.084 | 9.411 | 5.703 | 8.655 | 36.449 |
| 4 KiB | 34.656 | 9.667 | 31.437 | 11.317 | 31.331 | 108.337 |
| 16 KiB | 127.562 | 36.824 | 118.188 | 38.661 | 137.834 | 561.542 |
| 64 KiB | 714.53 | 184.695 | 529.886 | 163.428 | 534.026 | 1851.389 |
| 256 KiB | 2026.66 | 595.767 | 2339.97 | 599.264 | 2431.688 | 6722.005 |
