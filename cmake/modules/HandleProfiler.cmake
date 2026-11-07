function(add_profiling_target TARGET_NAME)
	if("$ENV{PROCESSOR_IDENTIFIER}" MATCHES "AuthenticAMD")
		set(UPROF_CONFIGS tbp hotspots inst_access energy cpi branch assess_ext)

		find_program(AMD_UPROF_CLI_EXE NAMES AMDuProfCLI)
		if(AMD_UPROF_CLI_EXE)
			foreach(CONF IN LISTS UPROF_CONFIGS)
				add_custom_target(${CONF}_${TARGET_NAME}
					COMMAND ${CMAKE_COMMAND} -E make_directory "${EXPORT_OUT_DIR}/profiling/ze_json"
					COMMAND ${AMD_UPROF_CLI_EXE} collect --config ${CONF} 
							-o ${CMAKE_BINARY_DIR}/${CONF}_${TARGET_NAME}
							-- $<$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:Release>>:$<TARGET_FILE:${TARGET_NAME}>>
					COMMAND ${AMD_UPROF_CLI_EXE} report 
							-i ${CMAKE_BINARY_DIR}/${CONF}_${TARGET_NAME}.cxldb
							-o ${EXPORT_OUT_DIR}/profiling/ze_json/${CONF}_${RUN_ID}.csv
							
					DEPENDS ${TARGET_NAME}
					COMMENT "Running AMD uProf (${CONF} on ${TARGET_NAME}) and generating CSV report..."
					VERBATIM
				)
			endforeach()
		endif()
	elseif("$ENV{PROCESSOR_IDENTIFIER}" MATCHES "GenuineIntel")
		find_program(VTUNE_EXE NAMES vtune)
	else()
		message(WARNING "vtune not found. Automated profiling will be disabled.")
	endif()
endfunction()