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

	find_program(LLVM_PROFDATA_EXE NAMES llvm-profdata)
	find_program(LLVM_COV_EXE NAMES llvm-cov)

	if(LLVM_PROFDATA_EXE AND LLVM_COV_EXE)
		set(PROFRAW_FILE "${CMAKE_BINARY_DIR}/${TARGET_NAME}.profraw")
		set(PROFDATA_FILE "${CMAKE_BINARY_DIR}/${TARGET_NAME}.profdata")

		add_custom_target(coverage_${TARGET_NAME}
			COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}/coverage/ze_json"
			COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${PROFRAW_FILE} $<TARGET_FILE:${TARGET_NAME}>
			COMMAND ${LLVM_PROFDATA_EXE} merge -sparse ${PROFRAW_FILE} -o ${PROFDATA_FILE}
			COMMAND ${LLVM_COV_EXE} report $<TARGET_FILE:${TARGET_NAME}> -instr-profile=${PROFDATA_FILE}
			COMMAND ${LLVM_COV_EXE} show $<TARGET_FILE:${TARGET_NAME}> -instr-profile=${PROFDATA_FILE} -format=txt -output-dir=${EXPORT_OUT_DIR}/coverage/ze_json/${TARGET_NAME}_${RUN_ID}
			DEPENDS ${TARGET_NAME}
			COMMENT "Running coverage pipeline and generating txt..."
			VERBATIM
		)
	endif()
endfunction()