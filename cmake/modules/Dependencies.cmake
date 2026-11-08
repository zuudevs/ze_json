find_package(Python COMPONENTS Interpreter REQUIRED)

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/templates/ValidateVersion.py.in" 
    "${CMAKE_BINARY_DIR}/generated/ValidateVersion.py" 
    @ONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/templates/version.hpp.in
    ${CMAKE_BINARY_DIR}/generated/ze_json/core/version.hpp
    @ONLY
)

add_custom_target(generate_ze_json_headers
    COMMAND ${Python_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/GenerateLookups.py" "${CMAKE_BINARY_DIR}/generated/ze_json/core/lookups.hpp"
    COMMENT "Generating required headers and validating version..."
    VERBATIM
)
if(Python_FOUND)
	add_custom_command(
		TARGET generate_ze_json_headers PRE_BUILD
		COMMAND ${Python_EXECUTABLE} "${CMAKE_BINARY_DIR}/generated/ValidateVersion.py"
		COMMENT "Running ValidateVersion.py before building target..."
		VERBATIM
	)

	if(NOT EXISTS ${JSON_CHECKER_INSTALLED_DIR})
		add_custom_command(
			TARGET generate_ze_json_headers PRE_BUILD
			COMMAND ${Python_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/InstallJsonChecker.py" 
				--package-dir ${JSON_CHECKER_INSTALLED_DIR} 
				--corpus-dir ${JSON_CHECKER_CORPUS_DIR}
			COMMENT "Running InstallJsonChecker.py before building target..."
			VERBATIM
		)
	endif()
endif()