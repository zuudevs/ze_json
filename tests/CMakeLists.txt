# Scanner differential + correctness tests (TASKS P1-3, P1-4).
#
# Rationale: every fast path must reproduce the plain scalar reference
# byte-for-byte (cpp-highperf-json rule 4; AGENTS.md section 6) before any
# performance claim is admissible. Corpus cases come from json.org
# JSON_checker (corpus/test/json_checker); legacy RFC 8259 exceptions are
# fail1.json (top-level string) and fail18.json (20-level nesting).
#
# @cite 10.14778_3115404.3115416 (Mison structural indexing)
# @cite 10.48550_arXiv.1902.08318 (simdjson staging)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
	# GTest/benchmark modules are native-only (root CMakeLists skips them for
	# wasm targets); nothing to register here.
	return()
endif()

set(ZE_JSON_CORPUS_DIR "${JSON_CHECKER_CORPUS_DIR}" CACHE PATH
	"json_checker corpus directory consumed by the scanner tests")

# Normalize separators: backslashes inside a C string literal would form
# invalid escape sequences (\t, \v, ...), corrupting the path.
string(REPLACE "\\" "/" ZE_JSON_CORPUS_DIR_POSIX "${ZE_JSON_CORPUS_DIR}")

add_executable(ze_json_scanner_test scanner_test.cpp)

target_include_directories(ze_json_scanner_test PRIVATE
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/benchmarks>
)

target_link_libraries(ze_json_scanner_test PRIVATE
	${LIBRARY_OUTPUT_NAME}
	GTest::gtest
)

target_compile_definitions(ze_json_scanner_test PRIVATE
	ZE_JSON_CORPUS_DIR="${ZE_JSON_CORPUS_DIR_POSIX}"
)

add_test(NAME ze_json_scanner_test COMMAND ze_json_scanner_test)

add_export_test_target(ze_json_scanner_test)
add_code_coverage_target(ze_json_scanner_test)

# Register the number parser tests (TASKS P7-2).
# @cite 10.1002_spe.2984 (number parsing at a gigabyte per second)
add_executable(ze_json_number_test number_test.cpp)

target_include_directories(ze_json_number_test PRIVATE
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/benchmarks>
)

target_link_libraries(ze_json_number_test PRIVATE
	${LIBRARY_OUTPUT_NAME}
	GTest::gtest
)

add_test(NAME ze_json_number_test COMMAND ze_json_number_test)

add_export_test_target(ze_json_number_test)
add_code_coverage_target(ze_json_number_test)

# ---------------------------------------------------------------------------
# UTF-8 validator differential + correctness tests (TASKS P7-1).
# @cite 10.1002_spe.2920 (Niagara two-table validation)
add_executable(ze_json_utf8_test utf8_test.cpp)

target_include_directories(ze_json_utf8_test PRIVATE
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/benchmarks>
)

target_link_libraries(ze_json_utf8_test PRIVATE
	${LIBRARY_OUTPUT_NAME}
	GTest::gtest
)

target_compile_definitions(ze_json_utf8_test PRIVATE
	ZE_JSON_CORPUS_DIR="${ZE_JSON_CORPUS_DIR_POSIX}"
)

add_test(NAME ze_json_utf8_test COMMAND ze_json_utf8_test)

add_export_test_target(ze_json_utf8_test)
add_code_coverage_target(ze_json_utf8_test)

# ---------------------------------------------------------------------------
# String token scan + unescape tests (TASKS P7-3).
# @cite 10.48550_arXiv.1902.08318 (simdjson string pass)
# @cite 10.14778_3115404.3115416 (Mison span decomposition)
add_executable(ze_json_string_test string_test.cpp)

target_include_directories(ze_json_string_test PRIVATE
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/benchmarks>
)

target_link_libraries(ze_json_string_test PRIVATE
	${LIBRARY_OUTPUT_NAME}
	GTest::gtest
)

target_compile_definitions(ze_json_string_test PRIVATE
	ZE_JSON_CORPUS_DIR="${ZE_JSON_CORPUS_DIR_POSIX}"
)

add_test(NAME ze_json_string_test COMMAND ze_json_string_test)

add_export_test_target(ze_json_string_test)
add_code_coverage_target(ze_json_string_test)

# ---------------------------------------------------------------------------
# On-demand Value/Document API tests (TASKS P7-4).
# @cite 10.1002_spe.3313 (on-demand JSON front-end semantics)
# @cite 10.14778_3115404.3115416 (Mison structural-index traversal)
add_executable(ze_json_value_test value_test.cpp)

target_include_directories(ze_json_value_test PRIVATE
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/benchmarks>
)

target_link_libraries(ze_json_value_test PRIVATE
	${LIBRARY_OUTPUT_NAME}
	GTest::gtest
)

add_test(NAME ze_json_value_test COMMAND ze_json_value_test)

add_export_test_target(ze_json_value_test)
add_code_coverage_target(ze_json_value_test)

# ---------------------------------------------------------------------------
# Reference parser verdict-differential + json_checker gate (TASKS P7-6).
# @cite 10.1002_spe.3313 (on-demand JSON — full-walk verdict anchor)
# @cite 10.14778_3115404.3115416 (Mison — correctness needs no index)
add_executable(ze_json_reference_parser_test reference_parser_test.cpp)

target_include_directories(ze_json_reference_parser_test PRIVATE
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/benchmarks>
)

target_link_libraries(ze_json_reference_parser_test PRIVATE
	${LIBRARY_OUTPUT_NAME}
	GTest::gtest
)

set(ZE_JSON_BENCH_CORPUS_DIR "$ENV{PROJECT_ROOT_DIR}/corpus/benchmarks" CACHE PATH
	"benchmark corpora directory (corpus/benchmarks, P0-8 manifests)")

# Normalize separators: backslashes inside a C string literal would form
# invalid escape sequences (\t, \v, ...), corrupting the path.
string(REPLACE "\\" "/" ZE_JSON_BENCH_CORPUS_DIR_POSIX
	"${ZE_JSON_BENCH_CORPUS_DIR}")

target_compile_definitions(ze_json_reference_parser_test PRIVATE
	ZE_JSON_CORPUS_DIR="${ZE_JSON_CORPUS_DIR_POSIX}"
	ZE_JSON_BENCH_CORPUS_DIR="${ZE_JSON_BENCH_CORPUS_DIR_POSIX}"
)

add_test(NAME ze_json_reference_parser_test
	COMMAND ze_json_reference_parser_test)

add_export_test_target(ze_json_reference_parser_test)
add_code_coverage_target(ze_json_reference_parser_test)