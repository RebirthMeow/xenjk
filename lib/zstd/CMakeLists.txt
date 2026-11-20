#============================================================================
# xenjk 2026-11-08: zstd's decoder, bundled (collision option C: the XBSZ chunk BSPs, codemp/qcommon/cm_zbsp.cpp).
#
# Upstream zstd 1.5.7 (github.com/facebook/zstd, release tarball zstd-1.5.7.tar.gz, sha256
# eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3), BSD (LICENSE) or GPLv2 (COPYING). Only the
# decoder: lib/zstd.h, lib/zstd_errors.h, lib/common's headers and the six sources the decoder needs, lib/decompress
# without its x86-64 assembly; the files are upstream's, unchanged. Built as a static library; a target that uses it
# adds ZSTD_INCLUDE_DIRS and ZSTD_LIBRARIES.
#============================================================================

set(ZSTDDir ${CMAKE_CURRENT_SOURCE_DIR})
add_library(bundled_zstd STATIC
	"${ZSTDDir}/common/debug.c"
	"${ZSTDDir}/common/entropy_common.c"
	"${ZSTDDir}/common/error_private.c"
	"${ZSTDDir}/common/fse_decompress.c"
	"${ZSTDDir}/common/xxhash.c"
	"${ZSTDDir}/common/zstd_common.c"
	"${ZSTDDir}/decompress/huf_decompress.c"
	"${ZSTDDir}/decompress/zstd_ddict.c"
	"${ZSTDDir}/decompress/zstd_decompress.c"
	"${ZSTDDir}/decompress/zstd_decompress_block.c"
	)
# no assembly (huf_decompress_amd64.S is not bundled), no legacy formats, no threads (the decoder needs none)
target_compile_definitions(bundled_zstd PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0 ZSTD_MULTITHREAD=0)
target_include_directories(bundled_zstd PRIVATE "${ZSTDDir}" "${ZSTDDir}/common")
set_target_properties(bundled_zstd PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(ZSTD_LIBRARIES bundled_zstd PARENT_SCOPE)
set(ZSTD_INCLUDE_DIRS ${ZSTDDir} PARENT_SCOPE)
mark_as_advanced(ZSTD_LIBRARIES ZSTD_INCLUDE_DIRS)
