# Benchmark hanya untuk target native (di luar wasm32/64) — lintas-OS;
# google-benchmark di-ambil via toolchain vcpkg dari preset native-*.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
	function(RunAndExportBenchmarkReport name executable_name)
		set(FILENAME_POSTFIX_ "")
		if(NOT GIT_COMMIT_HASH STREQUAL "unknown_hash")
			set(FILENAME_POSTFIX_ "_${GIT_COMMIT_HASH}")
		endif()
		
		set(RESULT_DIR "${CMAKE_SOURCE_DIR}/out/results/benchmark")
		add_custom_target(run_and_export_${name}_report
			COMMAND ${CMAKE_COMMAND} -E make_directory "${RESULT_DIR}"
			COMMAND $<TARGET_FILE:${executable_name}> 
				--benchmark_out=${RESULT_DIR}/${executable_name}${FILENAME_POSTFIX_}.json
				--benchmark_out_format=json
			DEPENDS ${executable_name}
			COMMENT "Run & Export Report for ${name}"
		)   
	endfunction()

	include("${CMAKE_SOURCE_DIR}/cmake/modules/optimizations.cmake")
	include("${CMAKE_SOURCE_DIR}/cmake/packages/google-benchmark.cmake")

	add_release_optimizations_target(${LIBRARY_OUTPUT_NAME})

	function(add_comprehensive_benchmark_target name)
		add_executable(${name}
			${ARGN}
		)

		target_include_directories(
			${name}
				PRIVATE
					${CMAKE_SOURCE_DIR}/include
		)

		target_link_libraries(
			${name}
				PRIVATE
					benchmark::benchmark_main
					${LIBRARY_OUTPUT_NAME}
		)

		add_release_optimizations_target(${name})
		RunAndExportBenchmarkReport(${name} ${name})
	endfunction()

	add_comprehensive_benchmark_target(scanner_benchmark
		"${CMAKE_CURRENT_SOURCE_DIR}/scanner_benchmark/main.cpp"
	)
	
	add_comprehensive_benchmark_target(parser_benchmark
		"${CMAKE_CURRENT_SOURCE_DIR}/parser_benchmark/main.cpp"
	)

	add_comprehensive_benchmark_target(session_benchmark
		"${CMAKE_CURRENT_SOURCE_DIR}/session_benchmark/main.cpp"
	)

	add_comprehensive_benchmark_target(span_iso_benchmark
		"${CMAKE_CURRENT_SOURCE_DIR}/span_iso_benchmark/main.cpp"
	)
endif()