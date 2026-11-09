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
