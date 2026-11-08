find_package(GTest CONFIG REQUIRED)

if(NOT GTest_FOUND)
	include(FetchContent)
	FetchContent_Declare(
		googletest
		GIT_REPOSITORY https://github.com/google/googletest.git
		GIT_TAG        v1.18.0
	)

	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

function(add_export_test_target TARGET_NAME)
	add_custom_target(export_test_${TARGET_NAME}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}/tests/ze_json"
		COMMAND ${CMAKE_COMMAND} -E env
			"ZE_METADATA_OUT=${OUT_DIR}/tests/ze_json/${TARGET_NAME}_${RUN_ID}.metadata.json"
			"ZE_RUN_ID=${RUN_ID}"
			"ZE_GIT_COMMIT=${GIT_COMMIT_HASH}"
			$<TARGET_FILE:${TARGET_NAME}>
			--gtest_output=json:${OUT_DIR}/tests/ze_json/${TARGET_NAME}_${RUN_ID}.json
		DEPENDS ${TARGET_NAME}
		COMMENT "Run & Export Report for ${TARGET_NAME} (run_id: ${RUN_ID})"
	)
endfunction()