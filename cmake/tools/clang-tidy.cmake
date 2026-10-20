find_program(CLANG_TIDY_EXE NAMES clang-tidy)

function(add_clang_tidy_target target)
	if(CLANG_TIDY_EXE)
		set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
	else()
		message(WARNING "clang tidy not found! Target '${target}' will not be created.")
	endif()
endfunction()