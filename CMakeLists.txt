cmake_minimum_required(VERSION 3.40)
project(
	ze_json
	VERSION 0.1.0
	LANGUAGES CXX C
)

include("cmake/modules/Config.cmake")
include("cmake/modules/Variable.cmake")
include("cmake/modules/HandleGit.cmake")
include("cmake/modules/Optimization.cmake")
include("cmake/modules/CheckTidy.cmake")
include("cmake/modules/CodeFormat.cmake")
include("cmake/modules/Sanitizer.cmake")

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
	include("cmake/modules/Testing.cmake")
	include("cmake/modules/Benchmark.cmake")
endif()

include("cmake/modules/Dependencies.cmake")
include("cmake/modules/HandleProfiler.cmake")
include("cmake/modules/CodeCoverage.cmake")
include("cmake/modules/HandleDoxygen.cmake")
include("cmake/modules/Packaging.cmake")

add_subdirectory("src")
add_subdirectory("tests")
add_subdirectory("benchmarks")

message("")
message("Project Name           : ${PROJECT_NAME}")
message("Project Version        : ${PROJECT_VERSION}")
message("Project Version Number : ${ZE_JSON_VERSION_NUMBER}")
message("Build Type             : ${CMAKE_BUILD_TYPE}")
message("C++ Standard           : ${CMAKE_CXX_STANDARD}")
message("C++ Compiler           : ${CMAKE_CXX_COMPILER}")
message("Compiler Architecture  : ${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
message("CMake Generator        : ${CMAKE_GENERATOR}")

if(DEFINED GIT_COMMIT_HASH)
	message("Git Commit Hash        : ${GIT_COMMIT_HASH}")
else()
	message(WARNING "Git Commit Hash not found!")
endif()

message("Run ID                 : ${RUN_ID}")

if(ENABLE_AVX512)
	message("AVX512 Support         : TRUE")
else()
	message("AVX512 Support         : FALSE")
endif()

message("")