if(${CMAKE_C_COMPILER_ARCHITECTURE_ID} STREQUAL "wasm32" OR ${CMAKE_C_COMPILER_ARCHITECTURE_ID} STREQUAL "wasm64")
    add_executable(${LIBRARY_OUTPUT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/memory/arena.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ondemand/document.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ondemand/json_array.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ondemand/json_object.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ondemand/json_value.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/scanner/scanner.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ze_json.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/align.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/alloc.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/utils/compiler_rt.cpp
    )

	target_include_directories(${LIBRARY_OUTPUT_NAME}
		PRIVATE
			${CMAKE_SOURCE_DIR}/include
	)

    target_link_options(${LIBRARY_OUTPUT_NAME} 
		PRIVATE
			-nostdlib
			"LINKER:--no-entry"
    )

	set_target_properties(${LIBRARY_OUTPUT_NAME} 
		PROPERTIES
        	SUFFIX ".wasm"
    )
elseif(${CMAKE_C_COMPILER_ARCHITECTURE_ID} STREQUAL "X86" OR ${CMAKE_C_COMPILER_ARCHITECTURE_ID} STREQUAL "x64")
	add_library(${LIBRARY_OUTPUT_NAME}
		${CMAKE_CURRENT_SOURCE_DIR}/memory/arena.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ondemand/document.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ondemand/json_array.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ondemand/json_object.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ondemand/json_value.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/scanner/scanner.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ze_json.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/align.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/alloc.cpp
	)

	target_include_directories(${LIBRARY_OUTPUT_NAME}
		PRIVATE
			${CMAKE_SOURCE_DIR}/include
	)
endif()