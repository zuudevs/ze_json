function(add_code_coverage_target TARGET_NAME)
	target_compile_options(${TARGET_NAME} PRIVATE 
		$<$<CONFIG:Debug>:-fprofile-instr-generate>
		$<$<CONFIG:Debug>:-fcoverage-mapping>
		$<$<CONFIG:Debug>:-O0>
		$<$<CONFIG:Debug>:-g>
	)
	target_link_options(${TARGET_NAME} PRIVATE 
		$<$<CONFIG:Debug>:-fprofile-instr-generate>
		$<$<CONFIG:Debug>:-fcoverage-mapping>
	)

	find_program(LLVM_PROFDATA NAMES llvm-profdata)
	find_program(LLVM_COV NAMES llvm-cov)

	if(LLVM_PROFDATA AND LLVM_COV)
		set(PROFRAW_FILE "${CMAKE_BINARY_DIR}/${TARGET_NAME}.profraw")
		set(PROFDATA_FILE "${CMAKE_BINARY_DIR}/${TARGET_NAME}.profdata")
		set(COVERAGE_HTML_DIR "${CMAKE_SOURCE_DIR}/out/results/coverage_html")

		include(${CMAKE_CURRENT_SOURCE_DIR}/git.cmake OPTIONAL)

		set(OUTDIR_SUFFIX "${GIT_COMMIT_HASH}")
		if(NOT OUTDIR_SUFFIX OR OUTDIR_SUFFIX STREQUAL "unknown_hash") 
			string(TIMESTAMP OUTDIR_SUFFIX "%Y-%m-%dT%H-%M-%SZ")
		endif()

		add_custom_target(coverage_${TARGET_NAME}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_HTML_DIR}
			COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${PROFRAW_FILE} $<TARGET_FILE:${TARGET_NAME}>
			COMMAND ${LLVM_PROFDATA} merge -sparse ${PROFRAW_FILE} -o ${PROFDATA_FILE}
			COMMAND ${LLVM_COV} report $<TARGET_FILE:${TARGET_NAME}> -instr-profile=${PROFDATA_FILE}
			COMMAND ${LLVM_COV} show $<TARGET_FILE:${TARGET_NAME}> -instr-profile=${PROFDATA_FILE} -format=html -output-dir=${COVERAGE_HTML_DIR}/${TARGET_NAME}_${OUTDIR_SUFFIX}
			DEPENDS ${TARGET_NAME}
			COMMENT "Running coverage pipeline and generating HTML..."
			VERBATIM
		)
	endif()
endfunction()