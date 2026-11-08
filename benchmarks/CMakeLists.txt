# Scanner throughput benchmarks (TASKS P1-5, P1-6).
#
# Rationale (synced with the record field `rationale`): measure the structural
# pre-scan (reference_scan vs table-driven scanner8) over the deterministic
# corpora from corpus/benchmarks (P0-8 manifests). One change per measurement;
# compare in-situ only (same machine, preset, corpus).
#
# @cite 10.14778_3115404.3115416 (Mison structural indexing)
# @cite 10.48550_arXiv.1902.08318 (simdjson staging)
# @cite 10.1145_2807591.2807644 (scientific benchmarking methodology)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
	# GTest/benchmark modules are native-only (root CMakeLists skips them for
	# wasm targets); nothing to register here.
	return()
endif()

set(ZE_JSON_BENCH_CORPUS_DIR "$ENV{PROJECT_ROOT_DIR}/corpus/benchmarks" CACHE PATH
	"benchmark corpora directory (corpus/benchmarks, P0-8 manifests)")

# Normalize separators: backslashes inside a C string literal would form
# invalid escape sequences (\t, \v, ...), corrupting the path.
string(REPLACE "\\" "/" ZE_JSON_BENCH_CORPUS_DIR_POSIX "${ZE_JSON_BENCH_CORPUS_DIR}")

add_executable(ze_json_scanner_benchmark scanner_benchmark.cpp)

target_include_directories(ze_json_scanner_benchmark PRIVATE
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
)

target_link_libraries(ze_json_scanner_benchmark PRIVATE
	${LIBRARY_OUTPUT_NAME}
	benchmark::benchmark
)

target_compile_definitions(ze_json_scanner_benchmark PRIVATE
	ZE_JSON_BENCH_CORPUS_DIR="${ZE_JSON_BENCH_CORPUS_DIR_POSIX}"
)

add_export_benchmark_target(ze_json_scanner_benchmark)
