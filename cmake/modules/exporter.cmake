function(add_export_benchmark_target TARGET_NAME)
	set(EXPORT_OUT_DIR ${CMAKE_SOURCE_DIR}/out/results/benchmark)

	include(${CMAKE_CURRENT_SOURCE_DIR}/git.cmake OPTIONAL)

	set(OUTFILE_SUFFIX "${GIT_COMMIT_HASH}")
	if(NOT OUTFILE_SUFFIX OR OUTFILE_SUFFIX STREQUAL "unknown_hash") 
		string(TIMESTAMP OUTFILE_SUFFIX "%Y-%m-%dT%H-%M-%SZ")
	endif()
	
	add_custom_target(export_benchmark_${TARGET_NAME}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${EXPORT_OUT_DIR}"
		COMMAND $<TARGET_FILE:${TARGET_NAME}> 
			--benchmark_out=${EXPORT_OUT_DIR}/${TARGET_NAME}_${OUTFILE_SUFFIX}.json
			--benchmark_out_format=json
		DEPENDS ${TARGET_NAME}
		COMMENT "Run & Export Report for ${TARGET_NAME}"
	)
endfunction()

function(add_export_test_target TARGET_NAME)
	set(EXPORT_OUT_DIR ${CMAKE_SOURCE_DIR}/out/results/test)

	include(${CMAKE_CURRENT_SOURCE_DIR}/git.cmake OPTIONAL)

	set(OUTFILE_SUFFIX "${GIT_COMMIT_HASH}")
	if(NOT OUTFILE_SUFFIX OR OUTFILE_SUFFIX STREQUAL "unknown_hash") 
		string(TIMESTAMP OUTFILE_SUFFIX "%Y-%m-%dT%H-%M-%SZ")
	endif()
	
	add_custom_target(export_test_${TARGET_NAME}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${EXPORT_OUT_DIR}"
		COMMAND $<TARGET_FILE:${TARGET_NAME}> 
			--gtest_output=json:${EXPORT_OUT_DIR}/${TARGET_NAME}_${OUTFILE_SUFFIX}.json
		DEPENDS ${TARGET_NAME}
		COMMENT "Run & Export Report for ${TARGET_NAME}"
	)
endfunction()