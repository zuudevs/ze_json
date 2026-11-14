# Doxygen API-documentation target (wired 2026-11-11, PR A).
#
# Template: cmake/Doxyfile.in is configured into the BUILD directory (never
# over the source tree) and the custom target runs doxygen there. Output:
# <build>/docs/doxygen/html (also visible to ctest/cpack packaging flows).
#
# Operational note: doxygen is looked up via find_program (host PATH); when
# absent the target is skipped with a warning instead of failing configure.
function(add_doxygen_target TARGET_NAME)
	find_program(DOXYGEN_EXE NAMES doxygen)
	if(NOT DOXYGEN_EXE)
		message(WARNING "Doxygen not found! Target '${TARGET_NAME}' will not be created.")
		return()
	endif()

	set(DOXYFILE_IN "${CMAKE_SOURCE_DIR}/cmake/templates/Doxyfile.in")
	set(DOXYFILE_OUT "${CMAKE_BINARY_DIR}/Doxyfile")
	set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs/doxygen")

	if(NOT EXISTS ${DOXYFILE_IN})
		message(WARNING "Doxyfile template not found at ${DOXYFILE_IN}! Target '${TARGET_NAME}' will not be created.")
		return()
	endif()

	configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

	add_custom_target(${TARGET_NAME}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${DOXYGEN_OUTPUT_DIRECTORY}"
		COMMAND ${DOXYGEN_EXE} "${DOXYFILE_OUT}"
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		COMMENT "Generating API documentation with Doxygen -> ${DOXYGEN_OUTPUT_DIRECTORY}/html"
		VERBATIM
	)
endfunction()
