if(${CMAKE_C_COMPILER_ARCHITECTURE_ID} STREQUAL "X86" OR ${CMAKE_C_COMPILER_ARCHITECTURE_ID} STREQUAL "x64")
	enable_testing()
	function(RunAndExportTestReport name executable_name)
		set(FILENAME_POSTFIX_ "")
		if(NOT GIT_COMMIT_HASH STREQUAL "unknown_hash")
			set(FILENAME_POSTFIX_ "_${GIT_COMMIT_HASH}")
		endif()
		
		set(RESULT_DIR "${CMAKE_SOURCE_DIR}/out/results/test")
		add_custom_target(run_and_export_${name}_report
			COMMAND ${CMAKE_COMMAND} -E make_directory "${RESULT_DIR}"
			COMMAND $<TARGET_FILE:${executable_name}> 
				--gtest_output=json:${RESULT_DIR}/${executable_name}${FILENAME_POSTFIX_}.json
			DEPENDS ${executable_name}
			COMMENT "Run & Export Report for ${name}"
		)
	endfunction()

	include("${CMAKE_SOURCE_DIR}/cmake/packages/google-test.cmake")
	include("${CMAKE_SOURCE_DIR}/cmake/modules/optimizations.cmake")

	function(add_test_target name)
		add_executable(${name}
			${ARGN}
		)

		target_include_directories(
			${name}
				PRIVATE
					"${CMAKE_SOURCE_DIR}/include"
		)

		target_link_libraries(
			${name}
				PRIVATE	
					GTest::gtest_main
					${LIBRARY_OUTPUT_NAME}
		)

		gtest_discover_tests(${name})
		RunAndExportTestReport(${name} ${name})
	endfunction()

	# Satu kelompok uji = satu folder (modular): scanner, document, object,
	# number, session, array, path.
	add_test_target(scanner_test
		"${CMAKE_CURRENT_SOURCE_DIR}/scanner_test/main.cpp"
	)

	add_test_target(document_test
		"${CMAKE_CURRENT_SOURCE_DIR}/document_test/main.cpp"
	)

	add_test_target(object_test
		"${CMAKE_CURRENT_SOURCE_DIR}/object_test/main.cpp"
	)

	add_test_target(number_test
		"${CMAKE_CURRENT_SOURCE_DIR}/number_test/main.cpp"
	)

	add_test_target(session_test
		"${CMAKE_CURRENT_SOURCE_DIR}/session_test/main.cpp"
	)

	add_test_target(array_test
		"${CMAKE_CURRENT_SOURCE_DIR}/array_test/main.cpp"
	)

	add_test_target(path_test
		"${CMAKE_CURRENT_SOURCE_DIR}/path_test/main.cpp"
	)

endif()