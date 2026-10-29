find_program(LLVM_PROFDATA NAMES llvm-profdata)
if(LLVM_PROFDATA)
    set(PROFDATA_FILE "${CMAKE_BINARY_DIR}/merged.profdata")
    
    if(NOT TARGET pgo_merge)
        add_custom_target(pgo_merge
            COMMAND ${LLVM_PROFDATA} merge -output=${PROFDATA_FILE} ${CMAKE_BINARY_DIR}/benchmarks/*.profraw
            COMMENT "Merging PGO profraw files into ${PROFDATA_FILE}..."
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif()

    if(NOT TARGET pgo_clean)
        add_custom_target(pgo_clean
            COMMAND ${CMAKE_COMMAND} -E remove -f ${PROFDATA_FILE}
            COMMENT "Cleaning PGO profdata file..."
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif()
endif()