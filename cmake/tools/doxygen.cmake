find_program(DOXYGEN_EXE NAMES DOXYGEN)

function(add_doxygen_target target)
	if(DOXYGEN_EXE)
		set(DOXYFILE_IN "${CMAKE_SOURCE_DIR}/cmake/templates/Doxyfile.in")
		set(DOXYFILE_OUT "${CMAKE_SOURCE_DIR}/Doxyfile")

		if(EXISTS ${DOXYFILE_IN})
			configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)
			
			add_custom_target(${target}
				COMMAND ${DOXYGEN_EXE} ${DOXYGEN_OUT}
				WORKING_DIRECTORY ${DOXYGEN_OUTPUT_DIRECTORY}
				COMMENT "Generating API documentation with Doxygen..."
				VERBATIM
			)
		else()
			add_custom_target(${target}
				COMMAND ${DOXYGEN_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
				WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
				COMMENT "Generating API documentation with Doxygen..."
				VERBATIM
			)
		endif()	
	else()
		message(WARNING "Doxygen not found! Target '${target}' will not be created.")
	endif()
endfunction()
