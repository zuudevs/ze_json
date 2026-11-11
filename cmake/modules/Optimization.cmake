include(${CMAKE_SOURCE_DIR}/cmake/modules/HandlePgo.cmake)

if(ENABLE_PGO)
    if(EXISTS "${CMAKE_BINARY_DIR}/merged.profdata")
        set(PGO_MODE "USE" CACHE STRING "PGO Mode" FORCE)
        message(STATUS "PGO Auto-Detected: 'merged.profdata' found -> PGO_MODE automatically set to USE")
    else()
        set(PGO_MODE "GENERATE" CACHE STRING "PGO Mode" FORCE)
        message(STATUS "PGO Auto-Detected: 'merged.profdata' not found -> PGO_MODE automatically set to GENERATE")
    endif()
else()
    set(PGO_MODE "OFF" CACHE STRING "PGO Mode" FORCE)
endif()

function(add_optimizations_target TARGET_NAME)
	# P4-4b: the full flag set below is OPT-IN — the canonical native-release
	# preset must keep CMake's default Release flag set (-O3 -DNDEBUG) so the
	# measurement baseline stays comparable (AGENTS.md section 4/6: never
	# compare across flag sets; -march=native only in local, labeled builds).
	if(NOT ENABLE_TARGET_OPTIMIZATIONS)
		return()
	endif()
	include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
	if(MSVC)
		target_compile_options(${TARGET_NAME} PRIVATE
			$<$<CONFIG:Release>:/O2>
			$<$<CONFIG:Release>:/Ob3>
			$<$<CONFIG:Release>:/Gy>
			$<$<CONFIG:Release>:/arch:AVX2>
			$<$<CONFIG:Release>:/EHs-c->
			$<$<CONFIG:Release>:/GR->
		)

		target_link_options(${TARGET_NAME} PRIVATE
			$<$<CONFIG:Release>:LINKER:/OPT:REF>
			$<$<CONFIG:Release>:LINKER:/OPT:ICF>
		)

	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
		if(PGO_MODE STREQUAL "GENERATE")
			message(STATUS "Optimizations: ${TARGET_NAME} PGO profile GENERATION")
			target_compile_options(${TARGET_NAME} PRIVATE
				$<$<CONFIG:Release>:-fprofile-generate>
				$<$<CONFIG:Release>:-mllvm>
				$<$<CONFIG:Release>:-vp-counters-per-site=8>
			)
			target_link_options(${TARGET_NAME} PRIVATE
				$<$<CONFIG:Release>:-fprofile-generate>
			)
		elseif(PGO_MODE STREQUAL "USE")
			message(STATUS "Optimizations: ${TARGET_NAME} PGO profile USE")
			target_compile_options(${TARGET_NAME} PRIVATE
				$<$<CONFIG:Release>:-fprofile-use=${CMAKE_BINARY_DIR}/merged.profdata>
			)
			target_link_options(${TARGET_NAME} PRIVATE
				$<$<CONFIG:Release>:-fprofile-use=${CMAKE_BINARY_DIR}/merged.profdata>
			)
		endif()

		target_compile_options(${TARGET_NAME} PRIVATE
			$<$<CONFIG:Release>:-ffunction-sections>
			$<$<CONFIG:Release>:-fdata-sections>
			$<$<CONFIG:Release>:-fno-exceptions>
			$<$<CONFIG:Release>:-fno-rtti>
		)

		# -march/-mtune=native stay behind their own opt-in: never in a
		# distributable build (AGENTS.md section 6); native-release-march
		# preset turns this on for local labeled measurements only.
		if(ENABLE_MARCH_NATIVE)
			target_compile_options(${TARGET_NAME} PRIVATE
				$<$<CONFIG:Release>:-march=native>
				$<$<CONFIG:Release>:-mtune=native>
			)
		endif()

		target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:-fprofile-instr-generate>
            $<$<CONFIG:Debug>:-fcoverage-mapping>
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Debug>:-g>
        )

        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:-fprofile-instr-generate>
            $<$<CONFIG:Debug>:-fcoverage-mapping>
        )

		if(WIN32 AND NOT MINGW)
			target_link_options(${TARGET_NAME} PRIVATE
				$<$<CONFIG:Release>:LINKER:/OPT:REF>
				$<$<CONFIG:Release>:LINKER:/OPT:ICF>
			)
		elseif(APPLE)
			target_link_options(${TARGET_NAME} PRIVATE
				$<$<CONFIG:Release>:LINKER:-dead_strip>
			)
		else()
			target_link_options(${TARGET_NAME} PRIVATE
				$<$<CONFIG:Release>:LINKER:--gc-sections>
			)
		endif()
	endif()

	if(IPO_SUPPORTED AND ENABLE_IPO)
		set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
	endif()
endfunction()
