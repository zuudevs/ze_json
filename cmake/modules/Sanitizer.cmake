if(ENABLE_SANITIZER)
	if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		message(WARNING 
			"ENABLE_SANITIZER only tested on native clang;"
			"gcc supported -fsanitize=address,undefined, but MSVC not supported."
		)
	endif()
	add_compile_options(
		-fsanitize=address,undefined
		-fno-omit-frame-pointer
		-g
	)
	add_link_options(
		-fsanitize=address,undefined
	)

	if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
		if(NOT DEFINED ASAN_RUNTIME_DLL)
			execute_process(
				COMMAND ${CMAKE_CXX_COMPILER} -print-resource-dir
				OUTPUT_VARIABLE CLANG_RESOURCE_DIR
				OUTPUT_STRIP_TRAILING_WHITESPACE
			)
			string(STRIP "${CLANG_RESOURCE_DIR}" CLANG_RESOURCE_DIR)
			find_file(ASAN_RUNTIME_DLL
				NAMES clang_rt.asan_dynamic-x86_64.dll
				PATHS "${CLANG_RESOURCE_DIR}/lib/windows"
				NO_DEFAULT_PATH
			)
		endif()
		if(NOT ASAN_RUNTIME_DLL)
			message(WARNING "clang_rt.asan_dynamic not found — binary sanitizer")
		endif()
	endif()

	function(sanitizer_copy_runtime TARGET_NAME)
		if(NOT ASAN_RUNTIME_DLL)
			return()
		endif()
		add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${ASAN_RUNTIME_DLL}"
				"$<TARGET_FILE_DIR:${TARGET_NAME}>"
			COMMENT "Copy clang ASan runtime next to ${TARGET_NAME}"
		)
	endfunction()
else()
	function(sanitizer_copy_runtime TARGET_NAME)
	endfunction()
endif()