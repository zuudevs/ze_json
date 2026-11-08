configure_file(
	"${CMAKE_SOURCE_DIR}/cmake/templates/ValidateVersion.py.in" 
	"${CMAKE_BINARY_DIR}/generated/ValidateVersion.py" 
	@ONLY
)

configure_file(
	${CMAKE_SOURCE_DIR}/cmake/templates/version.hpp.in
	${CMAKE_BINARY_DIR}/generated/version.hpp
	@ONLY
)

find_package(Python COMPONENTS Interpreter REQUIRED)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
    add_executable(
		${LIBRARY_OUTPUT_NAME} 
			${CMAKE_CURRENT_SOURCE_DIR}/dummy.cpp
	)

	target_include_directories(${LIBRARY_OUTPUT_NAME} PUBLIC
		$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
		$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
		$<INSTALL_INTERFACE:include>
	)

    target_link_options(${LIBRARY_OUTPUT_NAME}
		PRIVATE
			-nostdlib
			"LINKER:--no-entry"
			"LINKER:--global-base=3153920"
    )

	set_target_properties(${LIBRARY_OUTPUT_NAME} 
		PROPERTIES
        	SUFFIX ".wasm"
    )

	add_custom_command(
		TARGET ${LIBRARY_OUTPUT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:${LIBRARY_OUTPUT_NAME}>
			"${CMAKE_SOURCE_DIR}/bindings/js"
		COMMENT "Exporting ${LIBRARY_OUTPUT_NAME}.wasm artifact to bindings/js..."
	)
elseif(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
	add_library(
		${LIBRARY_OUTPUT_NAME} 
			${CMAKE_CURRENT_SOURCE_DIR}/dummy.cpp
	)

	target_include_directories(${LIBRARY_OUTPUT_NAME} PUBLIC
		$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
		$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
		$<INSTALL_INTERFACE:include>
	)
endif()

if(Python_FOUND)
    add_custom_command(
        TARGET ${LIBRARY_OUTPUT_NAME} PRE_BUILD
        COMMAND ${Python_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/GenerateLookups.py" "${CMAKE_BINARY_DIR}/generated/lookups.hpp"
        COMMENT "Running GenerateLookups.py before building target..."
        VERBATIM
    )

	if(NOT EXISTS ${JSON_CHECKER_INSTALLED_DIR} OR NOT EXISTS ${JSON_CHECKER_INSTALLED_DIR})
		add_custom_command(
			TARGET ${LIBRARY_OUTPUT_NAME} PRE_BUILD
			COMMAND ${Python_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/InstallJsonChecker.py" 
				--package-dir ${JSON_CHECKER_INSTALLED_DIR} 
				--corpus-dir ${JSON_CHECKER_CORPUS_DIR}
			COMMENT "Running InstallJsonChecker.py before building target..."
			VERBATIM
		)
	endif()

    add_custom_command(
        TARGET ${LIBRARY_OUTPUT_NAME} PRE_BUILD
        COMMAND ${Python_EXECUTABLE} "${CMAKE_BINARY_DIR}/generated/ValidateVersion.py"
        COMMENT "Running ValidateVersion.py before building target..."
        VERBATIM
    )
endif()