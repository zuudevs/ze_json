if(DEFINED ZUU_TOOLCHAIN_LOADED)
    return()
endif()
set(ZUU_TOOLCHAIN_LOADED TRUE)

if(NOT DEFINED ENV{TOOLCHAINS_DIR} OR "$ENV{TOOLCHAINS_DIR}" STREQUAL "")
    get_filename_component(ZUU_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    set(ENV{TOOLCHAINS_DIR} "${ZUU_ROOT_DIR}/.toolchains")
endif()

file(TO_CMAKE_PATH "$ENV{TOOLCHAINS_DIR}" TOOLCHAINS_DIR_CMAKE)

if(CMAKE_HOST_WIN32)
    set(ZUU_EXT ".exe")
else()
    set(ZUU_EXT "")
endif()

set(CMAKE_MAKE_PROGRAM "${TOOLCHAINS_DIR_CMAKE}/ninja/ninja${ZUU_EXT}" CACHE PATH "Path to make program" FORCE)
set(CMAKE_C_COMPILER "${TOOLCHAINS_DIR_CMAKE}/llvm/bin/clang${ZUU_EXT}" CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${TOOLCHAINS_DIR_CMAKE}/llvm/bin/clang++${ZUU_EXT}" CACHE FILEPATH "C++ compiler" FORCE)
set(CMAKE_RC_COMPILER "${TOOLCHAINS_DIR_CMAKE}/llvm/bin/llvm-rc${ZUU_EXT}" CACHE FILEPATH "RC compiler" FORCE)
set(CMAKE_LINKER "${TOOLCHAINS_DIR_CMAKE}/llvm/bin/lld${ZUU_EXT}" CACHE FILEPATH "Linker" FORCE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
    message(STATUS "[*] Target: WebAssembly (${CMAKE_SYSTEM_PROCESSOR})")

    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

    set(CMAKE_C_COMPILER_TARGET "${CMAKE_SYSTEM_PROCESSOR}-unknown-unknown")
    set(CMAKE_CXX_COMPILER_TARGET "${CMAKE_SYSTEM_PROCESSOR}-unknown-unknown")
else()
    message(STATUS "[*] Target: Native")

    get_filename_component(LLVM_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(LLVM_BIN_DIR "${LLVM_BIN_DIR}" CACHE INTERNAL "directory path LLVM binary")

    # Perbaikan Override: Menghapus set(CMAKE_LINKER "lld") yang merusak absolute path dari cache
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")

    find_program(CMAKE_CLANG_TIDY NAMES clang-tidy PATHS ${LLVM_BIN_DIR} NO_DEFAULT_PATH)
    find_program(CMAKE_CLANG_FORMAT NAMES clang-format PATHS ${LLVM_BIN_DIR} NO_DEFAULT_PATH)
endif()