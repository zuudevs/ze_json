set(LIBRARY_OUTPUT_NAME "${CMAKE_PROJECT_NAME}")
set(BINDING_DIR "${CMAKE_SOURCE_DIR}/bindings")

file(TO_CMAKE_PATH "$ENV{CORPUS_DIR}" CORPUS_DIR)
file(TO_CMAKE_PATH "$ENV{OUT_DIR}" OUT_DIR)
# Fallback: a configure without scripts/env/activate.ps1 used to cache an
# EMPTY OUT_DIR, which later writes exports/coverage to "/" (root of the
# drive). Default to the workspace out/ tree instead - behavior is
# unchanged whenever the env script is sourced.
if(NOT OUT_DIR)
	set(OUT_DIR "${CMAKE_SOURCE_DIR}/../out")
	message(STATUS "OUT_DIR env empty - defaulting to ${OUT_DIR}")
endif()
file(TO_CMAKE_PATH "$ENV{CACHE_DIR}" CACHE_DIR)

option(ENABLE_AVX512 "Enable feature for AVX512 compability" OFF)
option(ENABLE_PGO "Enable Profile-Guided Optimization" OFF)
option(ENABLE_SANITIZERS "Activated ASan + UBSan" OFF)
# P4-4b: the add_optimizations_target flag set (function/data sections,
# -fno-exceptions/-fno-rtti, linker GC) is opt-in so the canonical
# native-release baseline keeps CMake's default Release flags; -march=native
# is gated a second time (AGENTS.md section 6: local labeled builds only).
option(ENABLE_TARGET_OPTIMIZATIONS "Apply the add_optimizations_target flag set to wired targets" OFF)
option(ENABLE_MARCH_NATIVE "Add -march=native/-mtune=native (local labeled builds only)" OFF)
option(ENABLE_IPO "Enable interprocedural optimization (LTO) for wired targets" OFF)

set(JSON_CHECKER_INSTALLED_DIR "${CACHE_DIR}/json_checker")
set(JSON_CHECKER_CORPUS_DIR "$ENV{PROJECT_ROOT_DIR}/corpus/test/json_checker")
set(BIBLIOGRAPHY_DIR "$ENV{PROJECT_ROOT_DIR}/assets/bib")