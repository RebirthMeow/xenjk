# Bundled libogg for xenjk. Static library, decoder + encoder primitives.
# We bypass libogg's stock CMake (which expects autoconf-style config)
# and ship a hand-written config_types.h instead.

include_directories(include)

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_library(bundled_ogg STATIC
	src/bitwise.c
	src/framing.c
)

set(OGG_LIBRARIES   bundled_ogg PARENT_SCOPE)
set(OGG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/lib/ogg/include PARENT_SCOPE)
mark_as_advanced(OGG_LIBRARIES OGG_INCLUDE_DIR)

set_property(TARGET bundled_ogg PROPERTY POSITION_INDEPENDENT_CODE True)
