# Metrik Benchmark ze_json (`47e9cb7`)

Dibuat: 2026-10-23T13:35:44.692Z · preset: native-release

## parser_benchmark (47e9cb7, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T19:39:43+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| DocumentInitialization | 2.1 | 9.531.914.894 | 189982.0 MB/s |
| FindField_Early_And_ParseInt | 27.2 | 781.395.349 | 14798.3 MB/s |
| ParseDouble_Nested_Lemire | 168.4 | 118.938.053 | 2378.0 MB/s |
| Extreme_Deep_Search_SkipChild | 120.6 | 177.542.933 | 3330.2 MB/s |
| FindField_First_InWideObject | 20.3 | 1.058.267.717 | 395897.6 MB/s |
| FindField_Middle_InWideObject | 3995.6 | 5.250.000 | 2012.7 MB/s |
| FindField_Last_InWideObject | 8433.2 | 2.635.294 | 951.7 MB/s |
| GetString_PlainPayload | 27.9 | 750.837.989 | — |
| GetString_EscapedPayload | 25.0 | 850.632.911 | — |

## scanner_benchmark (47e9cb7, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T19:45:02+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| BM_Scanner8_Tiny | 215.4 | 98.102.190 | 747.4 MB/s |
| BM_Scanner8_Literal | 496.9 | 40.360.360 | 830.7 MB/s |
| BM_Scanner8_GithubEvents | 392849.7 | 51.298 | 829.1 MB/s |
| BM_Scanner8_UpdateCenter | 5604393.9 | 4.364 | 597.9 MB/s |
| BM_Scanner8_Jeopardy | 86628652.9 | 242 | 646.9 MB/s |
| BM_Scanner64_Tiny | 195.5 | 110.163.934 | 822.8 MB/s |
| BM_Scanner64_Literal | 498.4 | 42.397.476 | 831.6 MB/s |
| BM_Scanner64_GithubEvents | 246053.6 | 85.063 | 1323.1 MB/s |
| BM_Scanner64_UpdateCenter | 2181584.6 | 9.956 | 1518.3 MB/s |
| BM_Scanner64_Jeopardy | 36840000.4 | 567 | 1523.8 MB/s |
| BM_Scanner8_Synth | 297627.2 | 128.000 | 882.7 MB/s |
| BM_Scanner64_Synth | 246684.6 | 177.660 | 1067.6 MB/s |

## session_benchmark (47e9cb7, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T19:53:00+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| CreateSession_Pipeline | 263.5 | 85.605.096 | — |
| FindRead_SingleField | 330.0 | 67.537.688 | — |
| FindRead_ThreeFields | 357.3 | 56.708.861 | — |
| ArrayRead_AllElements | 418.9 | 49.411.765 | — |
| PathQuery_Flat | 39.8 | 576.824.034 | — |
| PathQuery_Nested | 115.7 | 181.621.622 | — |
| PathQuery_ArrayElement | 121.6 | 162.711.864 | — |
| PathQuery_Deep | 158.8 | 134.400.000 | — |
| PathQuery_ThreeTypes | 178.2 | 120.000.000 | — |
| KiB:0 | 1191.2 | 41.038.168 | — |
| KiB:1 | 18717.5 | 2.305.317 | — |
| KiB:2 | 76777.4 | 549.693 | — |
| KiB:3 | 297102.6 | 143.284 | — |
| KiB:4 | 614621.2 | 68.051 | — |
| Idx:2 | 94107.7 | 450.251 | — |
| Idx:4 | 776817.6 | 54.969 | — |
| Idx:2 | 74019.0 | 580.562 | — |
| Idx:4 | 605336.4 | 67.200 | — |
| WxK:0 | 37.5 | 956.583.630 | — |
| WxK:1 | 180.0 | 276.543.210 | — |
| WxK:2 | 324.6 | 100.000.000 | — |
| WxK:3 | 40.7 | 908.108.108 | — |
| WxK:4 | 35.5 | 1.000.000.000 | — |
| WxK:5 | 35.0 | 1.163.636.364 | — |
| WxK:6 | 43.6 | 977.454.545 | — |
| WxK:7 | 35.8 | 1.173.799.127 | — |
| WxK:8 | 36.2 | 1.168.695.652 | — |
| Idx:0 | 230.3 | 189.162.562 | — |
| Idx:1 | 3053.3 | 10.000.000 | — |
| Idx:2 | 22942.2 | 1.877.095 | — |
| Idx:0 | 205.3 | 205.190.840 | — |
| Idx:1 | 2297.5 | 17.766.028 | — |
| Idx:2 | 16433.7 | 2.353.765 | — |
| WxK:0 | 50.1 | 905.050.505 | — |
| WxK:1 | 78.4 | 537.600.000 | — |
| WxK:2 | 112.2 | 376.998.597 | — |
| WxK:3 | 46.7 | 905.050.505 | — |
| WxK:4 | 46.2 | 884.210.526 | — |
| WxK:5 | 46.4 | 905.050.505 | — |
| WxK:6 | 47.1 | 905.050.505 | — |
| WxK:7 | 44.9 | 939.860.140 | — |
| WxK:8 | 54.0 | 908.108.108 | — |
| KiB:0 | 19534.8 | 2.157.303 | — |
| KiB:1 | 81860.9 | 532.277 | — |
| Idx:2 | 65671.8 | 643.062 | — |
| Idx:4 | 547295.8 | 79.763 | — |
| KiB:0 | 33691.2 | 1.250.233 | — |
| KiB:1 | 135272.7 | 314.019 | — |

## Pengganda speed scanner64 vs scanner8 (per dataset)

| dataset | scanner8 (ns/op) | scanner64 (ns/op) | pengganda | scanner8 (MB/s) | scanner64 (MB/s) |
|---|---:|---:|---:|---:|---:|
| Tiny | 215.4 | 195.5 | 1.10× | 747.4 | 822.8 |
| Literal | 496.9 | 498.4 | 1.00× | 830.7 | 831.6 |
| GithubEvents | 392849.7 | 246053.6 | 1.60× | 829.1 | 1323.1 |
| UpdateCenter | 5604393.9 | 2181584.6 | 2.57× | 597.9 | 1518.3 |
| Jeopardy | 86628652.9 | 36840000.4 | 2.35× | 646.9 | 1523.8 |
| Synth | 297627.2 | 246684.6 | 1.21× | 882.7 | 1067.6 |

