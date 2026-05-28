# --- Project ---

cmake_minimum_required(VERSION 4.3.0)

set(COMMONLIB_PLUGIN_NAME "RobCoMigrator")
set(COMMONLIB_PLUGIN_AUTHOR "Miguel Raggi")
set(COMMONLIB_PLUGIN_VERSION "0.9.0.0")
set(COMMONLIB_PLUGIN_LICENSE "GPL-3.0 License")
set(COMMONLIB_PLUGIN_DESCRIPTION "Safely extract and format dynamic leveled list injections into RobCo Patcher files.")

string(TIMESTAMP COMMONLIB_PLUGIN_COPYRIGHT_YEAR "%Y" UTC)

project(
	"${COMMONLIB_PLUGIN_NAME}"
	VERSION "${COMMONLIB_PLUGIN_VERSION}"
	DESCRIPTION "${COMMONLIB_PLUGIN_DESCRIPTION}"
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LTO disabled: MSVC's c2.dll ICEs during whole-program code generation on this
# project. The DLL is tiny and called once per session, so LTO buys essentially
# nothing here. Re-enable if/when the compiler bug is fixed.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

include(FetchContent)
include(GNUInstallDirs)

# --- Options ---

set(COMMONLIB_RUNTIME_VR OFF)
set(COMMONLIB_RUNTIME_OG ON)
set(COMMONLIB_RUNTIME_NG ON)
set(COMMONLIB_RUNTIME_AE ON)

set(COMMONLIB_OPTION_FMT ON)
set(COMMONLIB_OPTION_XBYAK OFF)
set(COMMONLIB_OPTION_INI OFF)
set(COMMONLIB_OPTION_TOML OFF)
set(COMMONLIB_OPTION_JSON OFF)

# --- Add Sources ---

set(
	PCH_SOURCE
	"${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.hpp"
)

set(
	COMMONLIB_ROOT_DIR
	"${CMAKE_CURRENT_SOURCE_DIR}/lib/CommonLibF4"
)

include("${COMMONLIB_ROOT_DIR}/cmake/GenerateXsePluginInfo.cmake")
include("${COMMONLIB_ROOT_DIR}/cmake/SetXseOutputPath.cmake")
include("${COMMONLIB_ROOT_DIR}/cmake/CopyXseOutput.cmake")

generate_xse_plugin_info("${COMMONLIB_ROOT_DIR}")

file(
	GLOB_RECURSE
		SOURCES
	CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/*"
)

list(
	REMOVE_ITEM
		SOURCES
		"${PCH_SOURCE}"
)

source_group(
	TREE
		"${CMAKE_CURRENT_SOURCE_DIR}"
	FILES
		${SOURCES}
)

source_group(
	TREE
		"${CMAKE_CURRENT_BINARY_DIR}/include"
	FILES
		"${XSE_PLUGIN_INFO_CPP_TARGET}"
)

# --- Include ---

if(NOT TARGET CommonLibF4)
	add_subdirectory("${COMMONLIB_ROOT_DIR}" CommonLibF4)
endif()

add_library(
	"${PROJECT_NAME}"
	SHARED
		${SOURCES}
		"${XSE_PLUGIN_INFO_CPP_TARGET}"
		"${WIN32_VERSION_TARGET}"
)

target_include_directories(
	"${PROJECT_NAME}"
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_SOURCE_DIR}/include"
		"${CMAKE_CURRENT_BINARY_DIR}/include"
)

target_precompile_headers(
	"${PROJECT_NAME}"
	PRIVATE
		"${PCH_SOURCE}"
)

set_xse_output_path("${PROJECT_NAME}")

# --- Compile ---

target_compile_definitions(
	"${PROJECT_NAME}"
	PRIVATE
		"NOMINMAX"
		"WIN32_LEAN_AND_MEAN"
)

# --- Link ---

target_link_libraries(
	"${PROJECT_NAME}"
	PRIVATE
		CommonLibF4::CommonLibF4
)

# --- Copy Output ---

copy_xse_copy_output("${PROJECT_NAME}")
