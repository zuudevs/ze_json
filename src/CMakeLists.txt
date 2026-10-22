# Target WASM (wasm32/64): CMAKE_SYSTEM_PROCESSOR diset preset ku; ambungkan
# stub compiler-rt (untuk __multi3) di kejurungan wasm.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
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
	elseif(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
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