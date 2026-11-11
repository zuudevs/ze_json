# Coverage pipeline for the test executables (wired 2026-11-11, PR A).
#
# Flags mirror add_optimizations_target's Debug branch (profile-instr +
# coverage-mapping) so the instrumented Debug build also serves the thesis
# coverage question; the merged report lands in
# ${OUT_DIR}/coverage/ze_json/<target>_<run_id> (same OUT_DIR contract as the
# benchmark/export helpers via Config.cmake).
#
# Guard: instrumentation flags are Debug-only, so the custom target is
# registered only under CMAKE_BUILD_TYPE=Debug — a Release binary would run
# fine but produce empty coverage and llvm-cov errors (AGENTS.md section 3:
# Debug builds are the instrumented ones).
function(add_code_coverage_target TARGET_NAME)
	if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
		message(STATUS "Coverage instrumentation is Debug-only — coverage_${TARGET_NAME} skipped (build type: ${CMAKE_BUILD_TYPE})")
		return()
	endif()

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

	if(NOT LLVM_PROFDATA_EXE OR NOT LLVM_COV_EXE)
		message(WARNING "llvm-profdata/llvm-cov not found! coverage_${TARGET_NAME} will not be created.")
		return()
	endif()

	set(PROFRAW_FILE "${CMAKE_BINARY_DIR}/${TARGET_NAME}.profraw")
	set(PROFDATA_FILE "${CMAKE_BINARY_DIR}/${TARGET_NAME}.profdata")

	# NB (llvm-cov quirk, LLVM 23): (1) --output-dir with an absolute
	# Windows path gets mangled (drive prefix stripped, files land outside
	# the directory); (2) sources in the coverage mapping are relative to
	# the BUILD directory, so `show` only resolves them (and writes the
	# per-source reports) when its cwd is the build dir — from elsewhere it
	# exits 0 but writes nothing. Strategy proven by probe: run `show` from
	# ${CMAKE_BINARY_DIR} with a purely relative --output-dir, then copy the
	# result to the OUT_DIR destination via cmake -E (path handling we
	# control).
	set(COVERAGE_OUT_DIR "${OUT_DIR}/coverage/ze_json/${TARGET_NAME}_${RUN_ID}")

	add_custom_target(coverage_${TARGET_NAME}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${COVERAGE_OUT_DIR}"
		COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${PROFRAW_FILE} $<TARGET_FILE:${TARGET_NAME}>
		COMMAND ${LLVM_PROFDATA_EXE} merge -sparse ${PROFRAW_FILE} -o ${PROFDATA_FILE}
		COMMAND ${LLVM_COV_EXE} report $<TARGET_FILE:${TARGET_NAME}> --instr-profile=${PROFDATA_FILE}
		COMMAND ${LLVM_COV_EXE} show --object=$<TARGET_FILE:${TARGET_NAME}> --instr-profile=${PROFDATA_FILE} --output-dir=covshow/${TARGET_NAME}_${RUN_ID}
		COMMAND ${CMAKE_COMMAND} -E copy_directory covshow/${TARGET_NAME}_${RUN_ID} ${COVERAGE_OUT_DIR}
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
		DEPENDS ${TARGET_NAME}
		COMMENT "Running coverage pipeline and generating txt -> ${COVERAGE_OUT_DIR}"
		VERBATIM
	)
endfunction()
