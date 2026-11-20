#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__

/* xenjk: hand-written config_types.h that bypasses libogg's autoconf/cmake
 * machinery. Every modern toolchain has stdint.h (C99+, MSVC 2010+),
 * so we just use the standard types directly. */

#include <stdint.h>

typedef int16_t  ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t  ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t  ogg_int64_t;
typedef uint64_t ogg_uint64_t;

#endif
