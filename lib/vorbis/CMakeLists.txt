# Bundled libvorbis for xenjk: the core decoder only (TheoraPlay calls
# vorbis_synthesis_* and nothing from vorbisenc or vorbisfile).

include_directories(include lib ${OGG_INCLUDE_DIR})

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	# libvorbis uses many M_PI etc. - silence MSVC's noisy strict warnings
	add_definitions(/wd4244 /wd4267 /wd4305)
endif()

# Decoder + shared core (per upstream Makefile.am libvorbis_la_SOURCES)
add_library(bundled_vorbis STATIC
	lib/mdct.c lib/smallft.c lib/block.c lib/envelope.c lib/window.c
	lib/lsp.c  lib/lpc.c     lib/analysis.c lib/synthesis.c lib/psy.c
	lib/info.c lib/floor1.c  lib/floor0.c lib/res0.c lib/mapping0.c
	lib/registry.c lib/codebook.c lib/sharedbook.c lib/lookup.c
	lib/bitrate.c
)

set(VORBIS_LIBRARIES   bundled_vorbis PARENT_SCOPE)
set(VORBIS_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/lib/vorbis/include PARENT_SCOPE)
mark_as_advanced(VORBIS_LIBRARIES VORBIS_INCLUDE_DIR)

set_property(TARGET bundled_vorbis      PROPERTY POSITION_INDEPENDENT_CODE True)
