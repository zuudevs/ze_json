add_executable(json_checker_test
	"${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
)

target_include_directories(
	json_checker_test
		PRIVATE
			"${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(
	json_checker_test
		PRIVATE	
			GTest::gtest_main
			json_checker
			${LIBRARY_OUTPUT_NAME}
)

gtest_discover_tests(json_checker_test)
add_code_coverage_target(json_checker_test)
add_export_test_target(json_checker_test)