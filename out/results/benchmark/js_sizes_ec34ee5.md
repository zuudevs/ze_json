# Benchmark JS sapuan ukuran: JSON.parse vs ze_json — rev ec34ee5

Node v24.19.0, win32, 2026-10-23T23:18:34.740Z (benchmark.js v2)

Bentuk dokumen konstan; hanya lebar array items menskalakan. µs/op — lebih kecil = lebih cepat.

| ukuran | parse | ze scan | baca JSON | baca ze | iter JSON | iter ze |
|---|---|---|---|---|---|---|
| 95 B | 1.053 | 1.568 | 1.365 | 5.909 | 1.48 | 5.047 |
| 268 B | 3.792 | 2.433 | 2.719 | 5.833 | 2.78 | 13.998 |
| 1 KiB | 9.746 | 4.746 | 9.523 | 8.151 | 9.591 | 67.812 |
| 4 KiB | 37.891 | 14.663 | 51.145 | 22.612 | 43.387 | 246.165 |
| 16 KiB | 159.069 | 62.975 | 155.046 | 62.14 | 142.972 | 860.378 |
| 64 KiB | 614.65 | 216.635 | 527.182 | 199.737 | 502.319 | 3286.985 |
| 256 KiB | 3031.827 | 762.022 | 2758.704 | 829.418 | 2076.873 | 11903.319 |
