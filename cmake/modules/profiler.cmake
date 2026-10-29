function(add_profiling_target TARGET_NAME)
	set(UPROF_OUT_DIR ${CMAKE_SOURCE_DIR}/out/results/profiling)
	set(UPROF_CONFIGS tbp hotspots inst_access energy cpi branch assess_ext)

	find_program(AMD_UPROF_CLI NAMES AMDuProfCLI)
	if(AMD_UPROF_CLI)
		message(STATUS "Found AMD uProf CLI: ${AMD_UPROF_CLI}")

		include(${CMAKE_CURRENT_SOURCE_DIR}/git.cmake OPTIONAL)

		set(OUTFILE_SUFFIX "${GIT_COMMIT_HASH}")
		if(NOT OUTFILE_SUFFIX OR OUTFILE_SUFFIX STREQUAL "unknown_hash") 
			string(TIMESTAMP OUTFILE_SUFFIX "%Y-%m-%dT%H-%M-%SZ")
		endif()

		foreach(CONF IN LISTS UPROF_CONFIGS)
			add_custom_target(${CONF}_${TARGET_NAME}
				COMMAND ${CMAKE_COMMAND} -E make_directory ${UPROF_OUT_DIR}
				COMMAND ${AMD_UPROF_CLI} collect --config ${CONF} 
						-o ${CMAKE_BINARY_DIR}/${CONF}_${TARGET_NAME}
						-- $<$<OR:$<CONFIG:RelWithDebInfo>,$<CONFIG:Release>>:$<TARGET_FILE:${TARGET_NAME}>>
				COMMAND ${AMD_UPROF_CLI} report 
						-i ${CMAKE_BINARY_DIR}/${CONF}_${TARGET_NAME}.cxldb
						-o ${UPROF_OUT_DIR}/${CONF}_${OUTFILE_SUFFIX}.csv
						
				DEPENDS ${TARGET_NAME}
				COMMENT "Running AMD uProf (${CONF} on ${TARGET_NAME}) and generating CSV report..."
				VERBATIM
			)
		endforeach()
	else()
		message(WARNING "AMD uProf CLI not found. Automated profiling will be disabled.")
	endif()
endfunction()