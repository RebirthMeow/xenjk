# xenjk: FreeType for the UI module's RmlUi font engine (lib/rmlui). Static, no external dependencies (no zlib, bzip2,
# png, brotli or HarfBuzz); the modules are listed in include/freetype/config/ftmodule.h.

set(FreetypeSources
	src/autofit/autofit.c
	src/base/ftbase.c
	src/base/ftbbox.c
	src/base/ftbitmap.c
	src/base/ftdebug.c
	src/base/ftfstype.c
	src/base/ftgasp.c
	src/base/ftglyph.c
	src/base/ftinit.c
	src/base/ftmm.c
	src/base/ftstroke.c
	src/base/ftsynth.c
	src/base/ftsystem.c
	src/cff/cff.c
	src/gzip/ftgzip.c
	src/psaux/psaux.c
	src/pshinter/pshinter.c
	src/psnames/psnames.c
	src/sfnt/sfnt.c
	src/smooth/smooth.c
	src/truetype/truetype.c
	)

add_library(xenfreetype STATIC ${FreetypeSources})
target_include_directories(xenfreetype PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(xenfreetype PRIVATE FT2_BUILD_LIBRARY)
if(MSVC)
	target_compile_definitions(xenfreetype PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
	target_compile_options(xenfreetype PRIVATE /W0)
endif()
set_target_properties(xenfreetype PROPERTIES POSITION_INDEPENDENT_CODE ON PROJECT_LABEL "FreeType (xenui)")
