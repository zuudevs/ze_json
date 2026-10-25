configure_file(
	${CMAKE_SOURCE_DIR}/cmake/templates/version.hpp.in
	${CMAKE_SOURCE_DIR}/include/ze_json/version.hpp
	@ONLY
)

configure_file(
	${CMAKE_SOURCE_DIR}/cmake/templates/package.json.in
	${CMAKE_SOURCE_DIR}/js/package.json
	@ONLY
)

configure_file(
	${CMAKE_SOURCE_DIR}/cmake/templates/vcpkg.json.in
	${CMAKE_SOURCE_DIR}/vcpkg.json
	@ONLY
)