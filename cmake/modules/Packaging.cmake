set(CPACK_PACKAGE_NAME "ze_json")
set(CPACK_PACKAGE_VENDOR "zuudevs")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ze_json - on-demand JSON parser (C++23 core + C ABI + wasm binding)")
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL core js docs)
set(CPACK_PACKAGE_FILE_NAME "ze_json-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/ze_json"
	DESTINATION include
	COMPONENT core
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/src"
	DESTINATION "src"
	COMPONENT core
	FILES_MATCHING PATTERN "*.cpp" PATTERN "*.hpp"
)

install(FILES "${CMAKE_SOURCE_DIR}/bindings/js/ze_json.mjs"
	DESTINATION share/ze_json/js
	COMPONENT js
)

if(EXISTS "${CMAKE_SOURCE_DIR}/bindings/js/ze_json_core.wasm")
	install(FILES "${CMAKE_SOURCE_DIR}/bindings/js/ze_json_core.wasm"
		DESTINATION share/ze_json/js
		COMPONENT js
	)
endif()

install(FILES "${CMAKE_SOURCE_DIR}/README.md"
	DESTINATION share/ze_json
	COMPONENT docs
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/docs"
	DESTINATION share/ze_json
	COMPONENT docs
)

include(CPack)