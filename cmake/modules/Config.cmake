set(LIBRARY_OUTPUT_NAME "${CMAKE_PROJECT_NAME}")
set(BINDING_DIR "${CMAKE_SOURCE_DIR}/bindings")

file(TO_CMAKE_PATH "$ENV{CORPUS_DIR}" CORPUS_DIR)
file(TO_CMAKE_PATH "$ENV{OUT_DIR}" OUT_DIR)
file(TO_CMAKE_PATH "$ENV{CACHE_DIR}" CACHE_DIR)

option(ENABLE_AVX512 "Enable feature for AVX512 compability" OFF)
option(ENABLE_PGO "Enable Profile-Guided Optimization" OFF)
option(ENABLE_SANITIZERS "Activated ASan + UBSan" OFF)

set(JSON_CHECKER_INSTALLED_DIR "${CACHE_DIR}/json_checker")
set(JSON_CHECKER_CORPUS_DIR "$ENV{PROJECT_ROOT_DIR}/corpus/test/json_checker")