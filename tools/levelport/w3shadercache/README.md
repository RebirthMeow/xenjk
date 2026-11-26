# Witcher 3 shader cache tools

Read the game's own material shaders (`content/content0/shaderdx12_0.cache`, next-gen 4.x) to get the exact maths
w3scene.py and rd-vulkan's scene shader reproduce. `W3_GAME` names the game folder.

- `w3sc.py`: load the cache (blobs, index); run alone for a summary.
- `w3sc_map.py <out> <graph.w2mg>...`: extract the DXBC blobs a material graph's index entries name.
- `dxc -dumpbin <blob> > x.ll` (Vulkan SDK's dxc), then `dxil_pre.py x.ll y.ll` (names the dx.op unary/binary ops)
  and `dxil_simplify.py y.ll > x.ir` (one short line per op: CB<reg>, IN<n>, samples).

The decoded formulas and register names for the city's wall and roof graphs live with the extracted blobs
(levelport `w3/shadercache/FORMULAS.md`).
