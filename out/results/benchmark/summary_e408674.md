# Metrik Benchmark ze_json (`e408674`)

Dibuat: 2026-10-22T23:48:09.138Z · preset: native-release

## parser_benchmark (e408674, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T06:11:27+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| DocumentInitialization | 2.3 | 8.670.967.742 | 177118.6 MB/s |
| FindField_Early_And_ParseInt | 29.1 | 646.153.846 | 13924.6 MB/s |
| ParseDouble_Nested_Lemire | 178.5 | 137.142.857 | 2249.4 MB/s |
| Extreme_Deep_Search_SkipChild | 121.2 | 161.732.852 | 3311.6 MB/s |
| FindField_First_InWideObject | 20.9 | 1.148.717.949 | 388184.5 MB/s |
| FindField_Middle_InWideObject | 3913.9 | 5.250.000 | 2057.5 MB/s |
| FindField_Last_InWideObject | 7897.3 | 2.759.754 | 1021.2 MB/s |
| GetString_PlainPayload | 28.7 | 861.538.462 | — |
| GetString_EscapedPayload | 40.2 | 481.720.430 | — |

## scanner_benchmark (e408674, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T06:16:52+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| BM_Scanner8_Tiny | 201.7 | 105.000.000 | 806.6 MB/s |
| BM_Scanner8_Literal | 495.6 | 43.778.502 | 841.2 MB/s |
| BM_Scanner8_GithubEvents | 396471.8 | 56.000 | 828.1 MB/s |
| BM_Scanner8_UpdateCenter | 4675501.5 | 5.149 | 728.3 MB/s |
| BM_Scanner8_Jeopardy | 133755755.7 | 271 | 539.8 MB/s |
| BM_Scanner64_Tiny | 175.7 | 112.000.000 | 919.7 MB/s |
| BM_Scanner64_Literal | 645.8 | 32.230.216 | 759.0 MB/s |
| BM_Scanner64_GithubEvents | 252037.6 | 75.506 | 1300.6 MB/s |
| BM_Scanner64_UpdateCenter | 3201517.5 | 7.111 | 1060.6 MB/s |
| BM_Scanner64_Jeopardy | 47063150.0 | 390 | 1193.3 MB/s |
| BM_Scanner8_Synth | 271970.3 | 148.837 | 972.8 MB/s |
| BM_Scanner64_Synth | 215780.8 | 190.503 | 1231.7 MB/s |

## session_benchmark (e408674, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T06:25:43+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| CreateSession_Pipeline | 536.6 | 45.559.322 | — |
| FindRead_SingleField | 351.6 | 53.760.000 | — |
| FindRead_ThreeFields | 548.4 | 57.931.034 | — |
| ArrayRead_AllElements | 727.6 | 28.656.716 | — |
| PathQuery_Flat | 68.3 | 298.666.667 | — |
| PathQuery_Nested | 168.5 | 128.000.000 | — |
| PathQuery_ArrayElement | 177.0 | 99.555.556 | — |
| PathQuery_Deep | 249.1 | 88.421.053 | — |
| PathQuery_ThreeTypes | 210.6 | 105.000.000 | — |
| KiB:0 | 957.7 | 46.424.870 | — |
| KiB:1 | 14305.4 | 3.096.774 | — |
| KiB:2 | 57329.7 | 822.018 | — |
| KiB:3 | 230222.5 | 187.579 | — |
| KiB:4 | 459200.8 | 89.900 | — |
| Idx:2 | 73930.5 | 632.471 | — |
| Idx:4 | 575455.9 | 83.478 | — |
| Idx:2 | 58027.8 | 698.182 | — |
| Idx:4 | 601025.3 | 96.344 | — |
| WxK:0 | 43.2 | 875.570.033 | — |
| WxK:1 | 224.0 | 214.354.067 | — |
| WxK:2 | 377.0 | 111.074.380 | — |
| WxK:3 | 59.6 | 672.000.000 | — |
| WxK:4 | 46.2 | 887.128.713 | — |
| WxK:5 | 39.0 | 902.013.423 | — |
| WxK:6 | 44.4 | 984.615.385 | — |
| WxK:7 | 37.5 | 902.013.423 | — |
| WxK:8 | 39.6 | 1.200.000.000 | — |

## Pengganda speed scanner64 vs scanner8 (per dataset)

| dataset | scanner8 (ns/op) | scanner64 (ns/op) | pengganda | scanner8 (MB/s) | scanner64 (MB/s) |
|---|---:|---:|---:|---:|---:|
| Tiny | 201.7 | 175.7 | 1.15× | 806.6 | 919.7 |
| Literal | 495.6 | 645.8 | 0.77× | 841.2 | 759.0 |
| GithubEvents | 396471.8 | 252037.6 | 1.57× | 828.1 | 1300.6 |
| UpdateCenter | 4675501.5 | 3201517.5 | 1.46× | 728.3 | 1060.6 |
| Jeopardy | 133755755.7 | 47063150.0 | 2.84× | 539.8 | 1193.3 |
| Synth | 271970.3 | 215780.8 | 1.26× | 972.8 | 1231.7 |

