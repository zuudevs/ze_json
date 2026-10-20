cmake_minimum_required(VERSION 3.40)
project(
	zuu_json
	VERSION 0.1.0
	LANGUAGES CXX C
)

set(LIBRARY_OUTPUT_NAME "zejson_core")

if(MSVC)
    add_compile_options(
		/W4 
		/we4244 
		/arch:AVX512
	)
else()
    add_compile_options(
		-Wall 
		-Wextra 
		-Wconversion 
		-Wsign-conversion 
		-mavx2
		-msse2
		-Wunused-variable
	)
endif()

if(ENABLE_AVX512)
    add_compile_options(
		-mavx512f
		-mavx512bw
	)
endif()

include("${CMAKE_SOURCE_DIR}/cmake/tools/git.cmake")
add_subdirectory("${CMAKE_SOURCE_DIR}/src")
add_subdirectory("${CMAKE_SOURCE_DIR}/tests")
add_subdirectory("${CMAKE_SOURCE_DIR}/benchmarks")

find_program(CLANG_FORMAT_EXE NAMES clang-format)

if(CLANG_FORMAT_EXE)
	file(GLOB_RECURSE FORMAT_SRCS
		"${CMAKE_SOURCE_DIR}/src/*.cpp"
		"${CMAKE_SOURCE_DIR}/include/*.hpp"
		"${CMAKE_SOURCE_DIR}/tests/*.hpp"
		"${CMAKE_SOURCE_DIR}/tests/*.cpp"
		"${CMAKE_SOURCE_DIR}/benchmarks/*.hpp"
		"${CMAKE_SOURCE_DIR}/benchmarks/*.cpp"
	)

    if(FORMAT_SRCS)
        add_custom_target(format
            COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${FORMAT_SRCS}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			COMMENT "Formatting code with clang-format..."
        )

        add_custom_target(format-check
            COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror -style=file ${FORMAT_SRCS}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Checking source formatting"
        )
    else()
        message(WARNING "No source files found for clang-format target!")
    endif()
else()
    message(WARNING "clang-format not found! Format targets will not be created.")
endif()

message("")
message("Project Name          : ${PROJECT_NAME}")
message("Project Version       : ${PROJECT_VERSION}")
message("Build Type            : ${CMAKE_BUILD_TYPE}")
message("C++ Standard          : ${CMAKE_CXX_STANDARD}")
message("C++ Compiler          : ${CMAKE_CXX_COMPILER}")
message("COMPILER ARCHITECTURE : ${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
message("CMake Generator       : ${CMAKE_GENERATOR}")
message("Git Commit Hash       : ${GIT_COMMIT_HASH}")

if(ENABLE_AVX512)
	message("AVX512 Support        : TRUE")
else()
	message("AVX512 Support         : FALSE")
endif()

message("")