if(DEFINED ZUU_TOOLCHAIN_LOADED)
    return()
endif()
set(ZUU_TOOLCHAIN_LOADED TRUE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
    message(STATUS "[*] Target: WebAssembly (${CMAKE_SYSTEM_PROCESSOR})")

    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

    set(CMAKE_C_COMPILER_TARGET "${CMAKE_SYSTEM_PROCESSOR}-unknown-unknown")
    set(CMAKE_CXX_COMPILER_TARGET "${CMAKE_SYSTEM_PROCESSOR}-unknown-unknown")
else()
    message(STATUS "[*] Target: Native Windows x64 (Clang/Clang++)")

    set(CMAKE_SYSTEM_NAME Windows)

    get_filename_component(LLVM_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
	set(LLVM_BIN_DIR "${LLVM_BIN_DIR}" CACHE INTERNAL "directory path LLVM binary")

    set(CMAKE_RC_COMPILER "${LLVM_BIN_DIR}/llvm-rc.exe")
    set(CMAKE_LINKER "${LLVM_BIN_DIR}/lld.exe")

    set(CMAKE_LINKER "lld")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")

    find_program(CMAKE_CLANG_TIDY NAMES clang-tidy PATHS ${LLVM_BIN_DIR} NO_DEFAULT_PATH)
    find_program(CMAKE_CLANG_FORMAT NAMES clang-format PATHS ${LLVM_BIN_DIR} NO_DEFAULT_PATH)
endif()