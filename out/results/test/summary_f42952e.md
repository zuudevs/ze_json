# Metrik Pengujian ze_json (`f42952e`)

Dibuat: 2026-10-22T09:47:00.945Z · preset: native-debug, native-release

**Total: 56 test · 0 gagal · 0.05 s**

## Metrik per kelompok uji

| kelompok | preset | status | test | gagal | waktu (ms) | rata-rata (ms/test) |
|---|---|---|---:|---:|---:|---:|
| array_test | native-debug | PASSED | 3 | 0 | 9.000 | 3.000 |
| document_test | native-debug | PASSED | 2 | 0 | 7.000 | 3.500 |
| number_test | native-debug | PASSED | 3 | 0 | 8.000 | 2.667 |
| object_test | native-debug | PASSED | 5 | 0 | 8.000 | 1.600 |
| path_test | native-debug | PASSED | 6 | 0 | 10.000 | 1.667 |
| scanner_test | native-debug | PASSED | 5 | 0 | 0.000 | 0.000 |
| session_test | native-debug | PASSED | 4 | 0 | 7.000 | 1.750 |
| array_test | native-release | PASSED | 3 | 0 | 2.000 | 0.667 |
| document_test | native-release | PASSED | 2 | 0 | 0.000 | 0.000 |
| number_test | native-release | PASSED | 3 | 0 | 0.000 | 0.000 |
| object_test | native-release | PASSED | 5 | 0 | 1.000 | 0.200 |
| path_test | native-release | PASSED | 6 | 0 | 1.000 | 0.167 |
| scanner_test | native-release | PASSED | 5 | 0 | 0.000 | 0.000 |
| session_test | native-release | PASSED | 4 | 0 | 1.000 | 0.250 |

## 8 test paling memakan waktu

| # | kelompok | test | waktu (ms) |
|---:|---|---|---:|
| 1 | document_test | ParseAndFindFields | 4.000 |
| 2 | array_test | TraversalMixedElements | 3.000 |
| 3 | array_test | AmbiguousTapeCases | 2.000 |
| 4 | array_test | DrainContractAndForgiveness | 2.000 |
| 5 | document_test | ScanValidPayload | 2.000 |
| 6 | number_test | LemireBasicForms | 2.000 |
| 7 | number_test | IntegerBeyondDoublePrecision | 2.000 |
| 8 | number_test | ExponentBoundaries | 2.000 |
