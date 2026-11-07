set(LIBRARY_OUTPUT_NAME "${CMAKE_PROJECT_NAME}")
set(BINDING_DIR "${CMAKE_SOURCE_DIR}/bindings")

file(TO_CMAKE_PATH "$ENV{DATA_DIR}" DATA_DIR)
file(TO_CMAKE_PATH "$ENV{OUT_DIR}" OUT_DIR)

option(ENABLE_AVX512 "Enable feature for AVX512 compability" OFF)
option(ENABLE_PGO "Enable Profile-Guided Optimization" OFF)
option(ENABLE_SANITIZER "Activated ASan + UBSan" OFF)