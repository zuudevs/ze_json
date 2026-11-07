configure_file(
	"${CMAKE_SOURCE_DIR}/cmake/templates/ValidateVersion.py.in" 
	"${CMAKE_BINARY_DIR}/generated/ValidateVersion.py" 
	@ONLY
)

configure_file(
	${CMAKE_SOURCE_DIR}/cmake/templates/version.hpp.in
	${CMAKE_BINARY_DIR}/include/ze_json/core/version.hpp
	@ONLY
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
    add_executable(
		${LIBRARY_OUTPUT_NAME} 
			${CMAKE_CURRENT_SOURCE_DIR}/dummy.cpp
	)

	target_include_directories(${LIBRARY_OUTPUT_NAME} PUBLIC
		$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
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
		$<INSTALL_INTERFACE:include>
	)
endif()
