# Bundled libtheora for xenjk. DECODER ONLY (we never need to encode at
# runtime). encoder_disabled.c is a tiny stub that fulfils the
# th_encode_* symbols that decapiwrapper expects.

include_directories(include lib ${OGG_INCLUDE_DIR})

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	add_definitions(/wd4244 /wd4267 /wd4305 /wd4146)
endif()

# Tell libtheora's apiwrapper not to drag in the encoder paths.
add_definitions(-DTHEORA_DISABLE_ENCODE)

add_library(bundled_theora STATIC
	# The decoder set per upstream Makefile.am decoder_sources
	lib/apiwrapper.c
	lib/bitpack.c
	lib/decapiwrapper.c
	lib/decinfo.c
	lib/decode.c
	lib/dequant.c
	lib/fragment.c
	lib/huffdec.c
	lib/idct.c
	lib/info.c
	lib/internal.c
	lib/quant.c
	lib/state.c
	# Stub used when encoder is disabled
	lib/encoder_disabled.c
)

set(THEORA_LIBRARIES   bundled_theora PARENT_SCOPE)
set(THEORA_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/lib/theora/include PARENT_SCOPE)
mark_as_advanced(THEORA_LIBRARIES THEORA_INCLUDE_DIR)

set_property(TARGET bundled_theora PROPERTY POSITION_INDEPENDENT_CODE True)
