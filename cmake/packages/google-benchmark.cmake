find_package(benchmark CONFIG REQUIRED)

if(NOT benchmark_FOUND)
	include(FetchContent)
	FetchContent_Declare(
		googlebenchmark
		GIT_REPOSITORY https://github.com/google/benchmark.git
		GIT_TAG        v1.9.5
	)
	set(BENCHMARK_ENABLE_TEST OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(googlebenchmark)
endif()