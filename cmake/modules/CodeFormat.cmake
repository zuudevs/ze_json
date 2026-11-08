function(add_clang_format_target TARGET_NAME)
	find_program(CLANG_FORMAT_EXE NAMES clang-format)
	if(CLANG_FORMAT_EXE)
		get_target_property(SRCS ${TARGET_NAME} SOURCES)
		file(GLOB_RECURSE HEADERS 
			"${CMAKE_SOURCE_DIR}/include/*.h"
			"${CMAKE_SOURCE_DIR}/include/*.hpp"
		)
		if(SRCS AND HEADERS)
			add_custom_target(format_${TARGET_NAME}
				COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${SRCS} ${HEADERS}
				WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
				COMMENT "Formatting ${TARGET_NAME} with clang-format..."
			)
		else()
			message(WARNING "No source files found for clang-format target!")
		endif()
	else()
		message(WARNING "clang-format not found!")
	endif()
endfunction()