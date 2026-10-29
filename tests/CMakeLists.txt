# Uji hanya untuk target native (di luar wasm32/64) — lintas-OS:
# CMAKE_SYSTEM_PROCESSOR = wasm64/wasm32 bila sesuai preset WASM.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
	enable_testing()
	include("${CMAKE_SOURCE_DIR}/cmake/modules/optimizations.cmake")
	include(${CMAKE_SOURCE_DIR}/cmake/modules/coverage.cmake)
	include(${CMAKE_SOURCE_DIR}/cmake/modules/exporter.cmake)
	include("${CMAKE_SOURCE_DIR}/cmake/packages/google-test.cmake")

	function(add_test_target TARGET_NAME)
		add_executable(${TARGET_NAME}
			${ARGN}
		)

		target_include_directories(
			${TARGET_NAME}
				PRIVATE
					"${CMAKE_SOURCE_DIR}/include"
		)

		target_link_libraries(
			${TARGET_NAME}
				PRIVATE	
					GTest::gtest_main
					${LIBRARY_OUTPUT_NAME}
		)

		gtest_discover_tests(${TARGET_NAME})
	endfunction()

	add_test_target(scanner_test "${CMAKE_CURRENT_SOURCE_DIR}/scanner_test/main.cpp")
	add_code_coverage_target(scanner_test)
	add_export_test_target(scanner_test)

	add_test_target(document_test "${CMAKE_CURRENT_SOURCE_DIR}/document_test/main.cpp")
	add_code_coverage_target(document_test)
	add_export_test_target(document_test)

	add_test_target(object_test "${CMAKE_CURRENT_SOURCE_DIR}/object_test/main.cpp")
	add_code_coverage_target(object_test)
	add_export_test_target(object_test)

	add_test_target(number_test "${CMAKE_CURRENT_SOURCE_DIR}/number_test/main.cpp")
	add_code_coverage_target(number_test)
	add_export_test_target(number_test)

	add_test_target(session_test "${CMAKE_CURRENT_SOURCE_DIR}/session_test/main.cpp")
	add_code_coverage_target(session_test)
	add_export_test_target(session_test)

	add_test_target(array_test "${CMAKE_CURRENT_SOURCE_DIR}/array_test/main.cpp")
	add_code_coverage_target(array_test)
	add_export_test_target(array_test)

	add_test_target(path_test "${CMAKE_CURRENT_SOURCE_DIR}/path_test/main.cpp")
	add_code_coverage_target(path_test)
	add_export_test_target(path_test)

	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/JSON_checker_test")

endif()