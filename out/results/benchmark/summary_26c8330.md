# Metrik Benchmark ze_json (`26c8330`)

Dibuat: 2026-10-22T17:40:14.918Z · preset: native-release

## parser_benchmark (26c8330, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T00:12:42+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| DocumentInitialization | 1.9 | 11.389.830.508 | 212699.2 MB/s |
| FindField_Early_And_ParseInt | 23.9 | 884.210.526 | 16770.4 MB/s |
| ParseDouble_Nested_Lemire | 136.6 | 155.916.473 | 2928.8 MB/s |
| Extreme_Deep_Search_SkipChild | 104.5 | 202.409.639 | 3847.6 MB/s |
| FindField_First_InWideObject | 16.9 | 1.244.444.444 | 474238.4 MB/s |
| FindField_Middle_InWideObject | 3329.7 | 6.400.000 | 2410.1 MB/s |
| FindField_Last_InWideObject | 6957.2 | 3.125.581 | 1152.4 MB/s |
| GetString_PlainPayload | 22.5 | 939.860.140 | — |
| GetString_EscapedPayload | 23.9 | 884.210.526 | — |

## scanner_benchmark (26c8330, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T00:18:06+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| BM_Scanner8_Tiny | 135.9 | 158.677.686 | 1184.3 MB/s |
| BM_Scanner8_Literal | 286.0 | 73.846.154 | 1449.6 MB/s |
| BM_Scanner8_GithubEvents | 259217.9 | 83.478 | 1258.7 MB/s |
| BM_Scanner8_UpdateCenter | 3039831.4 | 6.964 | 1090.4 MB/s |
| BM_Scanner8_Jeopardy | 48896812.9 | 435 | 1144.8 MB/s |
| BM_Scanner64_Tiny | 116.2 | 180.888.291 | 1388.5 MB/s |
| BM_Scanner64_Literal | 267.4 | 77.687.861 | 1548.1 MB/s |
| BM_Scanner64_GithubEvents | 185246.0 | 112.941 | 1760.6 MB/s |
| BM_Scanner64_UpdateCenter | 2182680.2 | 9.669 | 1523.0 MB/s |
| BM_Scanner64_Jeopardy | 37648418.6 | 565 | 1492.5 MB/s |
| BM_Scanner8_Synth | 218700.2 | 192.826 | 1208.5 MB/s |
| BM_Scanner64_Synth | 191236.6 | 222.149 | 1381.9 MB/s |

## session_benchmark (26c8330, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T00:26:38+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| CreateSession_Pipeline | 198.9 | 107.520.000 | — |
| FindRead_SingleField | 258.2 | 83.478.261 | — |
| FindRead_ThreeFields | 299.6 | 70.366.492 | — |
| ArrayRead_AllElements | 346.4 | 61.935.484 | — |
| PathQuery_Flat | 29.6 | 707.368.421 | — |
| PathQuery_Nested | 89.7 | 235.376.532 | — |
| PathQuery_ArrayElement | 121.6 | 174.093.264 | — |
| PathQuery_Deep | 144.9 | 145.770.065 | — |
| PathQuery_ThreeTypes | 169.7 | 130.485.437 | — |
| KiB:0 | 810.4 | 51.891.892 | — |
| KiB:1 | 11969.9 | 3.550.859 | — |
| KiB:2 | 47392.6 | 887.129 | — |
| KiB:3 | 191631.8 | 220.690 | — |
| KiB:4 | 394534.9 | 107.092 | — |
| Idx:2 | 55073.9 | 774.640 | — |
| Idx:4 | 445887.3 | 94.648 | — |
| Idx:2 | 47615.4 | 887.129 | — |
| Idx:4 | 394343.8 | 107.520 | — |

## Pengganda speed scanner64 vs scanner8 (per dataset)

| dataset | scanner8 (ns/op) | scanner64 (ns/op) | pengganda | scanner8 (MB/s) | scanner64 (MB/s) |
|---|---:|---:|---:|---:|---:|
| Tiny | 135.9 | 116.2 | 1.17× | 1184.3 | 1388.5 |
| Literal | 286.0 | 267.4 | 1.07× | 1449.6 | 1548.1 |
| GithubEvents | 259217.9 | 185246.0 | 1.40× | 1258.7 | 1760.6 |
| UpdateCenter | 3039831.4 | 2182680.2 | 1.39× | 1090.4 | 1523.0 |
| Jeopardy | 48896812.9 | 37648418.6 | 1.30× | 1144.8 | 1492.5 |
| Synth | 218700.2 | 191236.6 | 1.14× | 1208.5 | 1381.9 |

