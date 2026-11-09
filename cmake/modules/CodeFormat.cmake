find_program(CLANG_FORMAT_EXE NAMES clang-format)
find_package(Python COMPONENTS Interpreter REQUIRED)
if(CLANG_FORMAT_EXE AND Python_FOUND)
	add_custom_target(formatting_sources
		COMMAND ${Python_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/FormatCode.py"
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		COMMENT "Formatting sources with clang-format..."
	)
else()
	message(WARNING "clang-format not found!")
endif()