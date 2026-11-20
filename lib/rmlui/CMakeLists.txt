# xenjk: RmlUi (Core and Debugger) for the UI module's xenui layer (codemp/ui/xenui*). Static, FreeType font engine
# (lib/freetype); no samples, backends, Lua, SVG or Lottie. Upstream builds the same files with its own CMake.

file(GLOB_RECURSE RmlUiCoreSources CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/*.h")
file(GLOB RmlUiDebuggerSources CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Debugger/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Debugger/*.h")

add_library(xenrmlui STATIC ${RmlUiCoreSources} ${RmlUiDebuggerSources})
target_include_directories(xenrmlui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Include")
target_compile_definitions(xenrmlui PUBLIC RMLUI_STATIC_LIB)
target_compile_definitions(xenrmlui PRIVATE RMLUI_FONT_ENGINE_FREETYPE "RMLUI_VERSION=\"6.3\"")
target_link_libraries(xenrmlui PRIVATE xenfreetype)
set_target_properties(xenrmlui PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON
	PROJECT_LABEL "RmlUi (xenui)")
target_precompile_headers(xenrmlui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/precompiled.h")
if(MSVC)
	target_compile_definitions(xenrmlui PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(xenrmlui PRIVATE /permissive- /MP /utf-8)
endif()

if(XenUIProfile)	# the zones become xenui's timers (codemp/ui/xenui_profile/tracy/Tracy.hpp; defined in xenui.cpp)
	target_compile_definitions(xenrmlui PUBLIC RMLUI_TRACY_PROFILING)
	target_include_directories(xenrmlui PUBLIC "${CMAKE_SOURCE_DIR}/codemp/ui/xenui_profile")
endif()
