# Vendored libraries added by this fork

OpenJK's own `lib/` libraries (SDL2, zlib, libpng, jpeg-9a, minizip, gsl-lite) are unchanged. The fork added the
Theora video decode chain (built only with `-DBuildTheora=ON`) and Detour. Each was trimmed on 2026-10-08 to the
sources its CMakeLists builds, their headers and the upstream notices (COPYING/LICENSE, CHANGES, AUTHORS, README); the
rest is in Git history before that date (docs/review/FINDINGS.md, area 9).

| Library | Path | Upstream version | License | Fork changes |
| --- | --- | --- | --- | --- |
| libogg | `lib/ogg` | 1.3.6 (2025-06-16), xiph git tree | BSD-3 (`COPYING`) | `CMakeLists.txt`; hand-written `include/ogg/config_types.h` (stdint types) |
| libvorbis | `lib/vorbis` | 1.3.7 (2020-07-04), xiph git tree | BSD-3 (`COPYING`) | `CMakeLists.txt` (decoder only; the encoder, codebooks and vorbisfile are not built) |
| libtheora | `lib/theora` | 1.2.0 (2025-03-29), xiph git tree | BSD-3 (`COPYING`) and On2's VP3 patent non-assertion (`LICENSE`) | `CMakeLists.txt` (decoder only, `THEORA_DISABLE_ENCODE`, C only) |
| TheoraPlay | `lib/theoraplay` | icculus snapshot, 2024 or later (exact commit unknown) | zlib (`LICENSE.txt`) | `CMakeLists.txt`; frame size capped at 8192 a side in `theoraplay.c` (marked `xenjk`) |
| Recast/Detour | `codemp/game/detour` | recastnavigation master at 9f4ce64 (v1.6.0-367), byte-identical | zlib (`License.txt`) | `DetourNavMeshBuilder` not vendored (nothing calls it) |

A binary distribution built with Theora must carry the Xiph BSD notices (the three `COPYING` files) in its
documentation.

## xenui (2026-11-05)

Built only into the MP UI module with `-DBuildXenUI=ON` (the default); see `docs/xenui-agents.md`.

| Library | Path | Upstream version | License | Fork changes |
| --- | --- | --- | --- | --- |
| RmlUi | `lib/rmlui` | 6.3 (tag `6.3`, ba95ffe, 2026-08-22), github.com/mikke89/RmlUi | MIT (`LICENSE.txt`) | `Include/` and `Source/Core`, `Source/Debugger` only; upstream CMake files replaced by one `CMakeLists.txt` (static, FreeType font engine); `Source/Core/ElementAnimation.cpp` copies a transform key before preparing it (marked `xenjk`: preparing resolved the style sheet's shared percentages to the first element's pixels, 2026-11-06) |
| FreeType | `lib/freetype` | 2.14.3 (tag `VER-2-14-3`, 0a0221a, 2026-03-22), github.com/freetype/freetype | FTL or GPLv2 (`LICENSE.TXT`, `docs/FTL.TXT`, `docs/GPLv2.TXT`) | `include/` and the `src/` modules for OpenType, TrueType and CFF, the auto-hinter and the smooth renderer; `include/freetype/config/ftmodule.h` lists only those (marked `xenjk`); own `CMakeLists.txt` (static, no zlib, bzip2, png, brotli or HarfBuzz) |

A binary distribution with xenui must carry the RmlUi MIT notice and the FreeType licence credit (FTL) in its
documentation.

## Collision chunk BSPs (2026-11-08)

Built into the engine and the dedicated server: `codemp/qcommon/cm_zbsp.cpp` decodes the XBSZ chunk BSPs
(docs/levelport-collision.md, section 38) with it, so no zstd library ships beside the binaries.

| Library | Path | Upstream version | License | Fork changes |
| --- | --- | --- | --- | --- |
| zstd | `lib/zstd` | 1.5.7, release tarball `zstd-1.5.7.tar.gz` (sha256 `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`), github.com/facebook/zstd | BSD (`LICENSE`) or GPLv2 (`COPYING`) | decoder only: `lib/zstd.h`, `lib/zstd_errors.h`, `lib/common` (headers and the six sources the decoder needs) and `lib/decompress` without its x86-64 assembly, byte-identical; own `CMakeLists.txt` (static, `ZSTD_DISABLE_ASM`, no legacy formats, no threads) |

A binary distribution must carry zstd's BSD notice (`lib/zstd/LICENSE`) in its documentation.
