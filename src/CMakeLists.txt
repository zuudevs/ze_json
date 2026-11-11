set(SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/stubs/compiler_rt.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/ondemand/scalar.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/ondemand/parallel.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/ondemand/number.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/ondemand/string.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/ondemand/document.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/abi/cabi.cpp
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
    add_executable(${LIBRARY_OUTPUT_NAME} ${SOURCES})

    # core::simd_* needs the stable wasm-simd128 proposal (shipped in all
    # engines since 2022); wasm_i8x16_* intrinsics require -msimd128.
    target_compile_options(${LIBRARY_OUTPUT_NAME} PRIVATE -msimd128)

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
			"LINKER:--export-memory"
			"LINKER:--export=__heap_base"
			"LINKER:--initial-memory=67108864"
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
	add_library(${LIBRARY_OUTPUT_NAME} ${SOURCES})

	target_include_directories(${LIBRARY_OUTPUT_NAME} PUBLIC
		$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
		$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
		$<INSTALL_INTERFACE:include>
	)

	# P4-4b: opt-in optimization flag set (full set only when
	# ENABLE_TARGET_OPTIMIZATIONS=ON — native-release stays canonical).
	add_optimizations_target(${LIBRARY_OUTPUT_NAME})

	add_clang_tidy_target(${LIBRARY_OUTPUT_NAME})
endif()

add_dependencies(${LIBRARY_OUTPUT_NAME} generate_ze_json_headers)