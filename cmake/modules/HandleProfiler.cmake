# AMD uProf (and Intel VTune placeholder) profiling targets — P4-6/P4-4b.
#
# Wiring note: `add_profiling_target(<target>)` registers one custom target
# per uProf predefined config, e.g. `ninja hotspots_ze_json_parser_benchmark`,
# which collects into <build>/<conf>_<target>/ and writes the CSV report to
# ${OUT_DIR}/profiling/ze_json/<conf>_<run_id>.csv (OUT_DIR comes from the
# environment via Config.cmake — the same location the export benchmark
# targets already use).
#
# Operational caveat (uProf 5.3 CLI): when stdout is redirected (non-tty),
# the CLI pages its output behind "Press any key..." prompts — run these
# targets from an interactive shell, or drive the CLI with a keystroke
# watchdog. Profiling numbers are for hotspot RANKING; the overhead makes
# them unusable as throughput measurements (AGENTS.md section 4).
#
# @cite 10.1109_PMBS56514.2022.00011 (unified hardware counter metrics).
function(add_profiling_target TARGET_NAME)
	if("$ENV{PROCESSOR_IDENTIFIER}" MATCHES "AuthenticAMD")
		set(UPROF_CONFIGS tbp hotspots inst_access energy cpi branch assess_ext)

		find_program(AMD_UPROF_CLI_EXE NAMES AMDuProfCLI)
		if(AMD_UPROF_CLI_EXE)
			foreach(CONF IN LISTS UPROF_CONFIGS)
				add_custom_target(${CONF}_${TARGET_NAME}
					COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}/profiling/ze_json"
					COMMAND ${AMD_UPROF_CLI_EXE} collect --config ${CONF}
							-o ${CMAKE_BINARY_DIR}/${CONF}_${TARGET_NAME}
							$<TARGET_FILE:${TARGET_NAME}>
					COMMAND ${AMD_UPROF_CLI_EXE} report
							-i ${CMAKE_BINARY_DIR}/${CONF}_${TARGET_NAME}
							-o ${OUT_DIR}/profiling/ze_json/${CONF}_${TARGET_NAME}_${RUN_ID}.csv

					DEPENDS ${TARGET_NAME}
					COMMENT "Running AMD uProf (${CONF}) on ${TARGET_NAME} -> ${OUT_DIR}/profiling/ze_json"
					VERBATIM
				)
			endforeach()
		else()
			message(STATUS "AMD uProf CLI not found — profiling targets for ${TARGET_NAME} skipped")
		endif()
	elseif("$ENV{PROCESSOR_IDENTIFIER}" MATCHES "GenuineIntel")
		find_program(VTUNE_EXE NAMES vtune)
		if(NOT VTUNE_EXE)
			message(STATUS "vtune not found. Automated profiling will be disabled.")
		endif()
	endif()
endfunction()
