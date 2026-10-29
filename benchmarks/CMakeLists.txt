# Benchmark hanya untuk target native (di luar wasm32/64) — lintas-OS;
# google-benchmark di-ambil via toolchain vcpkg dari preset native-*.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
	include("${CMAKE_SOURCE_DIR}/cmake/modules/optimizations.cmake")
	include("${CMAKE_SOURCE_DIR}/cmake/modules/profiler.cmake")
	include("${CMAKE_SOURCE_DIR}/cmake/modules/exporter.cmake")
	include("${CMAKE_SOURCE_DIR}/cmake/packages/google-benchmark.cmake")

	add_optimizations_target(${LIBRARY_OUTPUT_NAME})

	function(add_benchmark_target TARGET_NAME)
		add_executable(${TARGET_NAME}
			${ARGN}
		)

		target_include_directories(
			${TARGET_NAME}
				PRIVATE
					${CMAKE_SOURCE_DIR}/include
		)

		target_link_libraries(
			${TARGET_NAME}
				PRIVATE
					benchmark::benchmark_main
					${LIBRARY_OUTPUT_NAME}
		)

		add_optimizations_target(${TARGET_NAME})
	endfunction()

	add_benchmark_target(scanner_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/scanner_benchmark/main.cpp)
	add_export_benchmark_target(scanner_benchmark)
	add_profiling_target(scanner_benchmark)
	
	add_benchmark_target(parser_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/parser_benchmark/main.cpp)
	add_export_benchmark_target(parser_benchmark)
	add_profiling_target(parser_benchmark)

	add_benchmark_target(session_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/session_benchmark/main.cpp)
	target_compile_definitions(session_benchmark PUBLIC
		SAMPLE_DIR="${CMAKE_SOURCE_DIR}/samples/JSON_Checker"
	)
	add_export_benchmark_target(session_benchmark)
	add_profiling_target(session_benchmark)

	add_benchmark_target(span_iso_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/span_iso_benchmark/main.cpp)
	add_export_benchmark_target(span_iso_benchmark)
	add_profiling_target(span_iso_benchmark)
endif()