# Metrik Benchmark ze_json (`23a12ee`)

Dibuat: 2026-10-23T05:52:38.914Z · preset: native-release

## parser_benchmark (23a12ee, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T12:07:37+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| DocumentInitialization | 1.8 | 11.789.473.684 | 217452.2 MB/s |
| FindField_Early_And_ParseInt | 23.4 | 896.000.000 | 17044.7 MB/s |
| ParseDouble_Nested_Lemire | 137.2 | 153.951.890 | 2928.7 MB/s |
| Extreme_Deep_Search_SkipChild | 104.4 | 198.816.568 | 3845.3 MB/s |
| FindField_First_InWideObject | 17.1 | 1.256.074.766 | 470242.0 MB/s |
| FindField_Middle_InWideObject | 3406.8 | 6.280.374 | 2354.7 MB/s |
| FindField_Last_InWideObject | 6782.8 | 3.154.930 | 1184.6 MB/s |
| GetString_PlainPayload | 22.9 | 920.547.945 | — |
| GetString_EscapedPayload | 24.2 | 872.727.273 | — |

## scanner_benchmark (23a12ee, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T12:13:01+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| BM_Scanner8_Tiny | 133.4 | 158.677.686 | 1206.3 MB/s |
| BM_Scanner8_Literal | 288.2 | 73.846.154 | 1430.4 MB/s |
| BM_Scanner8_GithubEvents | 256810.3 | 81.455 | 1271.7 MB/s |
| BM_Scanner8_UpdateCenter | 2989896.4 | 7.149 | 1110.3 MB/s |
| BM_Scanner8_Jeopardy | 48314676.9 | 438 | 1161.3 MB/s |
| BM_Scanner64_Tiny | 117.4 | 178.961.385 | 1373.7 MB/s |
| BM_Scanner64_Literal | 272.9 | 78.596.491 | 1517.9 MB/s |
| BM_Scanner64_GithubEvents | 189231.6 | 112.000 | 1722.6 MB/s |
| BM_Scanner64_UpdateCenter | 2125780.9 | 9.956 | 1567.0 MB/s |
| BM_Scanner64_Jeopardy | 37056007.6 | 555 | 1516.8 MB/s |
| BM_Scanner8_Synth | 224324.1 | 187.841 | 1174.6 MB/s |
| BM_Scanner64_Synth | 193367.7 | 218.714 | 1366.2 MB/s |

## session_benchmark (23a12ee, release)

> host `ZUU` · CPU 2096 MHz × 12 core · benchmark lib v1.9.5 · 2026-10-23T12:21:28+07:00

| benchmark | ns/op | iterasi | throughput |
|---|---:|---:|---:|
| CreateSession_Pipeline | 194.8 | 108.387.097 | — |
| FindRead_SingleField | 261.1 | 82.962.963 | — |
| FindRead_ThreeFields | 301.4 | 71.489.362 | — |
| ArrayRead_AllElements | 354.8 | 60.000.000 | — |
| PathQuery_Flat | 36.5 | 576.824.034 | — |
| PathQuery_Nested | 100.4 | 210.658.307 | — |
| PathQuery_ArrayElement | 144.4 | 146.405.229 | — |
| PathQuery_Deep | 173.5 | 123.302.752 | — |
| PathQuery_ThreeTypes | 187.1 | 113.898.305 | — |
| KiB:0 | 821.6 | 51.395.793 | — |
| KiB:1 | 12243.1 | 3.419.847 | — |
| KiB:2 | 48693.6 | 872.727 | — |
| KiB:3 | 197822.8 | 216.600 | — |
| KiB:4 | 395978.7 | 106.667 | — |
| Idx:2 | 60383.5 | 701.828 | — |
| Idx:4 | 479290.8 | 88.713 | — |
| Idx:2 | 49111.4 | 853.333 | — |
| Idx:4 | 395123.0 | 107.520 | — |
| WxK:0 | 36.9 | 1.138.983.051 | — |
| WxK:1 | 158.6 | 264.566.929 | — |
| WxK:2 | 246.9 | 169.589.905 | — |
| WxK:3 | 36.3 | 1.148.717.949 | — |
| WxK:4 | 28.8 | 1.447.495.961 | — |
| WxK:5 | 28.9 | 1.457.700.651 | — |
| WxK:6 | 35.7 | 1.173.799.127 | — |
| WxK:7 | 29.4 | 1.418.469.657 | — |
| WxK:8 | 29.6 | 1.431.309.904 | — |
| Idx:0 | 190.3 | 221.234.568 | — |
| Idx:1 | 2392.0 | 17.672.584 | — |
| Idx:2 | 18787.8 | 2.199.673 | — |
| Idx:0 | 170.6 | 247.741.935 | — |
| Idx:1 | 2991.2 | 19.310.345 | — |
| Idx:2 | 18303.8 | 1.929.648 | — |

## Pengganda speed scanner64 vs scanner8 (per dataset)

| dataset | scanner8 (ns/op) | scanner64 (ns/op) | pengganda | scanner8 (MB/s) | scanner64 (MB/s) |
|---|---:|---:|---:|---:|---:|
| Tiny | 133.4 | 117.4 | 1.14× | 1206.3 | 1373.7 |
| Literal | 288.2 | 272.9 | 1.06× | 1430.4 | 1517.9 |
| GithubEvents | 256810.3 | 189231.6 | 1.36× | 1271.7 | 1722.6 |
| UpdateCenter | 2989896.4 | 2125780.9 | 1.41× | 1110.3 | 1567.0 |
| Jeopardy | 48314676.9 | 37056007.6 | 1.30× | 1161.3 | 1516.8 |
| Synth | 224324.1 | 193367.7 | 1.16× | 1174.6 | 1366.2 |

