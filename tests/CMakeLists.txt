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
					# ${LIBRARY_OUTPUT_NAME}
		)

		gtest_discover_tests(${name})
		RunAndExportTestReport(${name} ${name})
	endfunction()

	add_comprehensive_test_target(char_utils_test
		"${CMAKE_CURRENT_SOURCE_DIR}/char_utils_test.cpp"
	)

	add_comprehensive_test_target(scanner_test
		"${CMAKE_CURRENT_SOURCE_DIR}/scanner_test.cpp"
	)

endif()