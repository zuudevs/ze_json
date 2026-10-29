add_library(json_checker
	"${CMAKE_CURRENT_SOURCE_DIR}/JSON_checker.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/utf8_decode.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/utf8_to_utf16.c"
)

target_include_directories(json_checker PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
	$<INSTALL_INTERFACE:include>
)

target_compile_definitions(json_checker PUBLIC
	SAMPLE_DIR="${CMAKE_SOURCE_DIR}/samples/JSON_Checker"
)
