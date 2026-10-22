# Metrik Benchmark ze_json (`05d2180`)

Dibuat: 2026-10-22T10:29:32.018Z · preset: native-release

## parser_benchmark (05d2180, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-22T17:11:22+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| DocumentInitialization | 2.9 | 7.344.262.295 | 140339.9 MB/s |
| FindField_Early_And_ParseInt | 29.6 | 628.037.383 | 13568.6 MB/s |
| ParseDouble_Nested_Lemire | 157.1 | 108.387.097 | 2542.2 MB/s |
| Extreme_Deep_Search_SkipChild | 136.5 | 166.131.026 | 2978.0 MB/s |
| FindField_First_InWideObject | 18.3 | 1.138.983.051 | 438966.1 MB/s |
| FindField_Middle_InWideObject | 3756.5 | 5.600.000 | 2134.1 MB/s |
| FindField_Last_InWideObject | 7568.8 | 2.877.944 | 1058.8 MB/s |
| GetString_PlainPayload | 21.7 | 966.906.475 | — |
| GetString_EscapedPayload | 26.3 | 824.539.877 | — |

## scanner_benchmark (05d2180, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-22T17:16:40+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| BM_Scanner8_Tiny | 139.7 | 144.051.447 | 1147.9 MB/s |
| BM_Scanner8_Literal | 365.1 | 61.935.484 | 1129.8 MB/s |
| BM_Scanner8_GithubEvents | 272419.0 | 76.800 | 1191.0 MB/s |
| BM_Scanner8_UpdateCenter | 3463023.3 | 6.822 | 956.5 MB/s |
| BM_Scanner8_Jeopardy | 46944136.7 | 400 | 1192.1 MB/s |
| BM_Scanner64_Tiny | 125.7 | 178.961.385 | 1283.3 MB/s |
| BM_Scanner64_Literal | 379.5 | 73.442.623 | 1089.6 MB/s |
| BM_Scanner64_GithubEvents | 245309.1 | 82.963 | 1322.2 MB/s |
| BM_Scanner64_UpdateCenter | 2586914.3 | 8.453 | 1282.6 MB/s |
| BM_Scanner64_Jeopardy | 40061144.2 | 480 | 1392.0 MB/s |

## session_benchmark (05d2180, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-22T17:22:51+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| CreateSession_Pipeline | 238.1 | 93.333.333 | — |
| FindRead_SingleField | 288.1 | 59.207.048 | — |
| FindRead_ThreeFields | 411.5 | 49.051.095 | — |
| ArrayRead_AllElements | 523.1 | 37.229.917 | — |
| PathQuery_Flat | 40.7 | 555.371.901 | — |
| PathQuery_Nested | 125.6 | 169.911.504 | — |
| PathQuery_ArrayElement | 154.3 | 126.792.453 | — |
| PathQuery_Deep | 165.5 | 130.485.437 | — |
| PathQuery_ThreeTypes | 208.1 | 111.074.380 | — |

## Pengganda speed scanner64 vs scanner8 (per dataset)

| dataset | scanner8 (ns/op) | scanner64 (ns/op) | pengganda | scanner8 (MB/s) | scanner64 (MB/s) |
|---|---:|---:|---:|---:|---:|
| Tiny | 139.7 | 125.7 | 1.11× | 1147.9 | 1283.3 |
| Literal | 365.1 | 379.5 | 0.96× | 1129.8 | 1089.6 |
| GithubEvents | 272419.0 | 245309.1 | 1.11× | 1191.0 | 1322.2 |
| UpdateCenter | 3463023.3 | 2586914.3 | 1.34× | 956.5 | 1282.6 |
| Jeopardy | 46944136.7 | 40061144.2 | 1.17× | 1192.1 | 1392.0 |

