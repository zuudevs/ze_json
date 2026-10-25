include("${CMAKE_SOURCE_DIR}/cmake/modules/generator.cmake")
# Target WASM (wasm32/64): CMAKE_SYSTEM_PROCESSOR diset preset ku; ambungkan
# stub compiler-rt (untuk __multi3) di kejurungan wasm.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
    add_executable(${LIBRARY_OUTPUT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/memory/arena.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ondemand/document.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ondemand/json_array.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ondemand/json_object.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ondemand/json_value.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/scanner/scalar.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/scanner/parallel.cpp
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
			# Layout memori tetap (dikontrak js/ze_json.mjs):
			#   0x000000..0x000400 scratch + keys
			#   0x000400..0x100400 payload (berpotensi di-tulis JS sampai cap)
			#   0x100400..0x200400 tape
			#   0x200400..0x300400 arena
			# --global-base menempatkan STACK + segmen Data (.rodata) DI ATAS
			# seluruh layout tersebut — tanpa ini, penulisan payload lebih
			# dari ~63,9 KB menyapu .rodata (kStructural, kUtf8Dfa, dsb.) dan
			# scan pada payload besar melapor INVALID_UTF8 palsu.
			# 0x302000 ditulis desimal 3153920 — wasm-ld menolak literal hex.
			"LINKER:--global-base=3153920"
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
		${CMAKE_CURRENT_SOURCE_DIR}/scanner/scalar.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/scanner/parallel.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ze_json.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/align.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/alloc.cpp
	)

	target_include_directories(${LIBRARY_OUTPUT_NAME}
		PRIVATE
			${CMAKE_SOURCE_DIR}/include
	)
endif()