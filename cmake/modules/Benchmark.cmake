find_package(benchmark CONFIG REQUIRED)

if(NOT benchmark_FOUND)
	include(FetchContent)
	FetchContent_Declare(
		googlebenchmark
		GIT_REPOSITORY https://github.com/google/benchmark.git
		GIT_TAG        v1.9.5
	)
	set(BENCHMARK_ENABLE_TEST OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(googlebenchmark)
endif()

function(add_export_benchmark_target TARGET_NAME)
	add_custom_target(export_benchmark_${TARGET_NAME}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}/benchmarks/ze_json"
		COMMAND ${CMAKE_COMMAND} -E env
			"METADATA_OUT=${OUT_DIR}/benchmarks/ze_json/${TARGET_NAME}_${RUN_ID}.metadata.json"
			"RUN_ID=${RUN_ID}"
			"GIT_COMMIT=${GIT_COMMIT_HASH}"
			$<TARGET_FILE:${TARGET_NAME}>
			--benchmark_out=${OUT_DIR}/benchmarks/ze_json/${TARGET_NAME}_${RUN_ID}.json
			--benchmark_out_format=json
			--benchmark_context=schema_version=1.0.0
			--benchmark_context=run_id=${RUN_ID}
			--benchmark_context=git_commit=${GIT_COMMIT_HASH}
			--benchmark_context=build_type=${CMAKE_BUILD_TYPE}
			--benchmark_context=ze_json_version=${ZE_JSON_VERSION_NUMBER}
		DEPENDS ${TARGET_NAME}
		COMMENT "Run & Export Report for ${TARGET_NAME} (run_id: ${RUN_ID})"
	)
endfunction()