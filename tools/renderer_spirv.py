"""Rebuild rd-vulkan's embedded SPIR-V (shaders/spirv/shader_data.c, shader_binding.c) without the Vulkan SDK.

    python tools/renderer_spirv.py build  [--tree <rd-vulkan dir>]   # compile every shader, rewrite both files
    python tools/renderer_spirv.py verify [--tree <rd-vulkan dir>]   # compile, compare with the committed files only
    python tools/renderer_spirv.py split  [--tree <rd-vulkan dir>]   # no compile: rewrite shader_data.c as parts

shader_data.c is an index that includes shader_data_<n>.c, each under 45 MB (the arrays together are over 100 MB of C,
past what a git host takes in one file); `split` converts a single-file shader_data.c, such as compile.bat writes.

The default compiler is Khronos' glslang (prebuilt Windows release from github.com/KhronosGroup/glslang, 16.6.0,
the 16.x zip ships the tool as bin/glslang.exe, the same CLI as glslangValidator). Set env GLSLANG_EXE to its path, or
put glslangValidator / glslang on PATH (glslangValidator is preferred when both are). It is invoked as JKSunny's
shaders/tools/compile_threaded.cpp does: `-S <stage> -V -o <out.spv> <file> -D...`, one subprocess per shader on a
thread pool. glslang does `#include` (GL_GOOGLE_include_directive) and `-D` itself and writes SPIR-V 1.0 for -V, so
none of the rewriting below applies on this path; geometry shaders (filtercube.geom) compile too. Same task list,
names, order and C formatting as compile_threaded.cpp. `cloth_*` and `xenui_*` files are skipped (tools/cloth_spirv.py,
tools/xenui_spirv.py).

`--compiler godot` (or a missing glslang, with a message) falls back to Godot 4's RenderingDevice, which carries an
older glslang and compiles GLSL from a script (`shader_compile_spirv_from_source`). There `#include`s are inlined
here and `-D` defines become `#define`s after the `#version` line, and Godot's SPIR-V 1.3 is turned back into 1.0
(header version word; storage buffers from StorageBuffer/Block to Uniform/BufferBlock). Godot cannot compile geometry
shaders, so their committed bytes are kept on that path. A few shaders can differ from the committed bytes by
newer-glslang code generation. Godot is env GODOT_EXE or `godot` on PATH.

`verify` compares with the committed bytes, ignoring the header's version and generator words and the order of
decorations. `build` keeps the committed bytes of every shader that verifies as equal, so only edited shaders
(and any that differ by compiler version) change. Godot opens a tiny window off screen (about 2 s); no GPU use.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TREE = ROOT / 'codemp' / 'rd-vulkan'
WORK = Path(tempfile.gettempdir()) / 'xenjk-renderer-spirv'


def _tool(env, *names):
    """Path from env var `env`, else the first of `names` found on PATH, else the first name (reported missing)."""
    if os.environ.get(env):
        return Path(os.environ[env])
    for n in names:
        w = shutil.which(n)
        if w:
            return Path(w)
    return Path(names[0])


GODOT = _tool('GODOT_EXE', 'godot', 'godot4')
GLSLANG = _tool('GLSLANG_EXE', 'glslangValidator', 'glslang')

GD_SCRIPT = r'''extends SceneTree
func _init():
	var rd := RenderingServer.create_local_rendering_device()
	if rd == null:
		printerr("no rendering device")
		quit(2)
		return
	var jobs = JSON.parse_string(FileAccess.get_file_as_string(OS.get_environment("GDC_JOBS")))
	var failed := 0
	for job in jobs:
		var src := RDShaderSource.new()
		src.language = RenderingDevice.SHADER_LANGUAGE_GLSL
		var text := FileAccess.get_file_as_string(job["src"])
		var st := RenderingDevice.SHADER_STAGE_VERTEX
		if job["stage"] == "vert":
			src.source_vertex = text
		elif job["stage"] == "frag":
			src.source_fragment = text
			st = RenderingDevice.SHADER_STAGE_FRAGMENT
		else:
			src.source_compute = text
			st = RenderingDevice.SHADER_STAGE_COMPUTE
		var spv := rd.shader_compile_spirv_from_source(src, false)
		var err := spv.get_stage_compile_error(st)
		var code := spv.get_stage_bytecode(st)
		if err != "" or code.size() == 0:
			printerr("FAILED ", job["name"], ": ", err)
			failed += 1
			continue
		var f := FileAccess.open(job["out"], FileAccess.WRITE)
		f.store_buffer(code)
		f.close()
	print("compiled ", jobs.size() - failed, " of ", jobs.size())
	rd.free()
	quit(1 if failed else 0)
'''


def tasks(glsl):
    """(file, stage, var name, binding or None, defines) in compile_threaded.cpp's order."""
    out = []
    for stage in ('vert', 'frag', 'geom', 'comp'):
        for f in sorted(glsl.glob('*.' + stage), key=lambda p: p.name.lower()):
            if f.name.startswith(('cloth_', 'xenui_')):   # compiled separately: tools/cloth_spirv.py, tools/xenui_spirv.py
                continue
            out.append((f.name, stage, '%s_%s_spv' % (f.stem, stage), None, ''))
    t = lambda *a: out.append(a)
    t('gen_vert.tmpl', 'vert', 'vert_tx0_ident', None, '-DUSE_CL0_IDENT')
    t('gen_frag.tmpl', 'frag', 'frag_tx0_ident', None, '-DUSE_CL0_IDENT')
    t('gen_frag.tmpl', 'frag', 'frag_tx0_df', None, '-DUSE_ATEST -DUSE_DF')
    t('light_vert.tmpl', 'vert', 'vert_light', None, '')
    t('light_vert.tmpl', 'vert', 'vert_light_fog', None, '-DUSE_FOG')
    t('light_frag.tmpl', 'frag', 'frag_light', None, '')
    t('light_frag.tmpl', 'frag', 'frag_light_fog', None, '-DUSE_FOG')
    t('light_frag.tmpl', 'frag', 'frag_light_line', None, '-DUSE_LINE')
    t('light_frag.tmpl', 'frag', 'frag_light_line_fog', None, '-DUSE_LINE -DUSE_FOG')
    t('surface_sprite_vert.tmpl', 'vert', 'vert_surface_sprites', None, '')
    t('surface_sprite_vert.tmpl', 'vert', 'vert_surface_sprites_fog', None, '-DUSE_FOG')
    t('surface_sprite_frag.tmpl', 'frag', 'frag_surface_sprites', None, '-DUSE_ATEST')
    t('surface_sprite_frag.tmpl', 'frag', 'frag_surface_sprites_fog', None, '-DUSE_ATEST -DUSE_FOG')
    t('depth_extract.tmpl', 'frag', 'frag_depth_extract', None, '')
    t('depth_extract.tmpl', 'frag', 'frag_depth_extract_msaa', None, '-DUSE_MSAA')
    vbo = [('', ''), ('-DUSE_VBO_GHOUL2', 'ghoul2_'), ('-DUSE_VBO_MDV', 'mdv_')]
    fastlight = [('', ''), ('-DUSE_FAST_LIGHT', 'fastlight_')]
    light = [('', ''), ('-DUSE_LIGHTMAP', 'lightmap_'), ('-DUSE_LIGHT_VECTOR', 'vector_'), ('-DUSE_LIGHT_VERTEX', 'vertex_')]
    tx = [('', 'tx0'), ('-DUSE_TX1', 'tx1'), ('-DUSE_TX2', 'tx2')]
    cl = [('', ''), ('-DUSE_CL1', 'cl'), ('-DUSE_CL2', 'cl')]
    env = [('', ''), ('-DUSE_ENV', '_env')]
    fog = [('', ''), ('-DUSE_FOG', '_fog')]
    fog_only = [('-DUSE_FOG_LINEAR', 'linear'), ('-DUSE_FOG_EXP', 'exp')]
    join = lambda *f: ''.join(x + ' ' for x in f if x)
    for i, (vf, vi) in enumerate(vbo):
        for j, (ff, fi) in enumerate(fog_only):
            t('fog_vert.tmpl', 'vert', 'vert_fog_only_' + vi + fi, 'vk.shaders.vert.fog[%d][%d]' % (i, j), join(vf, ff))
    for i, (ff, fi) in enumerate(fog_only):
        t('fog_frag.tmpl', 'frag', 'frag_fog_only_' + fi, 'vk.shaders.frag.fog[%d]' % i, join(ff))
    for i, (vf, vi) in enumerate(vbo):
        t('refraction.tmpl', 'vert', 'refraction_' + vi, 'vk.shaders.refraction_vs[%d]' % i, join(vf))
        for n, (nf, ni) in enumerate(fastlight):
            for o, (of, oi) in enumerate(light):
                for j, (jf, ji) in enumerate(tx):
                    for k, (kf, ki) in enumerate(env):
                        for l, (lf, li) in enumerate(fog):
                            t('gen_vert.tmpl', 'vert', 'vert_' + vi + ni + oi + ji + ki + li,
                              'vk.shaders.vert.gen[%d][%d][%d][%d][0][%d][%d]' % (i, n, o, j, k, l), join(vf, nf, of, jf, kf, lf))
                            if j:
                                t('gen_vert.tmpl', 'vert', 'vert_' + vi + ni + oi + ji + '_' + cl[j][1] + ki + li,
                                  'vk.shaders.vert.gen[%d][%d][%d][%d][1][%d][%d]' % (i, n, o, j, k, l),
                                  join(vf, nf, of, jf, cl[j][0], kf, lf))
    for n, (nf, ni) in enumerate(fastlight):
        for o, (of, oi) in enumerate(light):
            for j, (jf, ji) in enumerate(tx):
                for k, (kf, ki) in enumerate(fog):
                    d = join(nf, of, jf, kf)
                    if j == 0:
                        d += ' -DUSE_ATEST'
                    t('gen_frag.tmpl', 'frag', 'frag_' + ni + oi + ji + ki, 'vk.shaders.frag.gen[%d][%d][%d][0][%d]' % (n, o, j, k), d)
                    if j == 0:   # r_g2SampleShading 2: per-sample alpha test as the sample mask; bound by vk_shaders.cpp
                        t('gen_frag.tmpl', 'frag', 'frag_' + ni + oi + ji + '_mask' + ki, None, d + ' -DUSE_SAMPLE_MASK')
                    if j:
                        t('gen_frag.tmpl', 'frag', 'frag_' + ni + oi + ji + '_' + cl[j][1] + ki,
                          'vk.shaders.frag.gen[%d][%d][%d][1][%d]' % (n, o, j, k), join(nf, of, jf, cl[j][0], kf))
    return out


INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*$')


def inline(path, glsl, depth=0):
    if depth > 16:
        raise SystemExit('include depth exceeded at %s' % path)
    lines = []
    for line in path.read_text(encoding='utf-8').splitlines():
        m = INCLUDE_RE.match(line)
        if m:
            inc = path.parent / m.group(1)
            if not inc.exists():
                inc = glsl / m.group(1)
            lines.append(inline(inc, glsl, depth + 1))
        else:
            lines.append(line)
    return '\n'.join(lines)


def preprocess(glsl, name, defines):
    text = inline(glsl / name, glsl)
    defs = ['#define %s' % d[2:].replace('=', ' ', 1) for d in defines.split() if d.startswith('-D')]
    lines = text.split('\n')
    for idx, line in enumerate(lines):
        if line.strip().startswith('#version'):
            return '\n'.join(lines[:idx + 1] + defs + lines[idx + 1:]) + '\n'
    raise SystemExit('%s has no #version line' % name)


def compile_glslang(tree, todo):
    """glslang CLI as compile_threaded.cpp calls it, in parallel; returns {var: SPIR-V 1.0 bytes}."""
    glsl = tree / 'shaders' / 'glsl'
    spv_dir = WORK / 'spv-glslang'
    spv_dir.mkdir(parents=True, exist_ok=True)
    for old in spv_dir.glob('*.spv'):
        old.unlink()   # regenerated outputs of this tool, not evidence

    def run(task):
        name, stage, var, binding, defines = task
        outp = spv_dir / (var + '.spv')
        cmd = [str(GLSLANG), '-S', stage, '-V', '-o', str(outp), name] + defines.split()
        r = subprocess.run(cmd, cwd=str(glsl), capture_output=True, text=True, timeout=300)
        return var, outp, r

    out, failed, seen = {}, [], set()
    uniq = [t for t in todo if not (t[2] in seen or seen.add(t[2]))]
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for var, outp, r in ex.map(run, uniq):
            if r.returncode != 0 or not outp.exists():
                failed.append('FAILED %s: %s' % (var, (r.stdout + r.stderr).strip()))
            else:
                out[var] = outp.read_bytes()
    if failed:
        print(chr(10).join(failed[:20]))
        raise SystemExit('glslang failed on %d shaders' % len(failed))
    return out


def compile_all(tree, compiler='glslang'):
    glsl = tree / 'shaders' / 'glsl'
    jobs, todo = [], tasks(glsl)
    if compiler == 'glslang':
        if GLSLANG.exists():
            return todo, compile_glslang(tree, todo)
        print('glslang not found at %s (set GLSLANG_EXE); falling back to Godot' % GLSLANG)
    WORK.mkdir(parents=True, exist_ok=True)
    src_dir, spv_dir = WORK / 'src', WORK / 'spv'
    src_dir.mkdir(exist_ok=True)
    spv_dir.mkdir(exist_ok=True)
    for old in spv_dir.glob('*.spv'):
        old.unlink()   # regenerated outputs of this tool, not evidence
    for name, stage, var, binding, defines in todo:
        if stage == 'geom':
            continue
        src = src_dir / (var + '.' + stage)
        src.write_text(preprocess(glsl, name, defines), encoding='utf-8', newline='\n')
        jobs.append({'name': var, 'stage': stage, 'src': str(src), 'out': str(spv_dir / (var + '.spv'))})
    (WORK / 'jobs.json').write_text(json.dumps(jobs), encoding='utf-8')
    (WORK / 'project.godot').write_text('config_version=5\n[application]\nconfig/name="spirv"\n', encoding='utf-8')
    (WORK / 'compile.gd').write_text(GD_SCRIPT, encoding='utf-8', newline='\n')
    env = dict(os.environ, GDC_JOBS=str(WORK / 'jobs.json'))
    r = subprocess.run([str(GODOT), '--rendering-driver', 'vulkan', '--windowed', '--resolution', '64x64',
                        '--position', '-4000,-4000', '--audio-driver', 'Dummy', '--path', str(WORK), '--script',
                        'compile.gd'], env=env, capture_output=True, text=True, timeout=600)
    errors = [l for l in (r.stdout + r.stderr).splitlines() if 'FAILED' in l or 'ERROR' in l.upper() and 'leaked' not in l]
    if r.returncode != 0:
        print('\n'.join(errors) or r.stdout + r.stderr)
        raise SystemExit('godot exited %d' % r.returncode)
    out = {}
    for name, stage, var, binding, defines in todo:
        if stage == 'geom':
            out[var] = None
            continue
        out[var] = to_spirv10((spv_dir / (var + '.spv')).read_bytes())
    return todo, out


def to_spirv10(data):
    """SPIR-V 1.3 -> 1.0 as glslangValidator -V wrote it for a Vulkan 1.0 instance: version word 1.0, and storage
    buffers in the Uniform storage class with BufferBlock (1.3 has a StorageBuffer class and Block instead)."""
    words = [int.from_bytes(data[i:i + 4], 'little') for i in range(0, len(data), 4)]
    words[1] = 0x00010000
    ssbo_types, i = set(), 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 0xffff
        if op == 32 and words[i + 2] == 12:      # OpTypePointer StorageBuffer
            words[i + 2] = 2
            ssbo_types.add(words[i + 3])
        elif op == 59 and words[i + 3] == 12:    # OpVariable StorageBuffer
            words[i + 3] = 2
        i += n
    i = 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 0xffff
        if op == 71 and words[i + 1] in ssbo_types and words[i + 2] == 2:   # OpDecorate Block -> BufferBlock
            words[i + 2] = 3
        elif op == 10:   # OpExtension: the storage-buffer class is no longer used
            name = b''.join(w.to_bytes(4, 'little') for w in words[i + 1:i + n]).split(b'\0')[0]
            if name == b'SPV_KHR_storage_buffer_storage_class':
                raise SystemExit('unexpected OpExtension %s' % name)
        i += n
    return b''.join(w.to_bytes(4, 'little') for w in words)


ARRAY_RE = re.compile(r'const unsigned char (\w+)\[(\d+)\] = \{\n(.*?)\n\};\n', re.S)


PART_RE = re.compile(r'#include "(shader_data_\d+\.c)"')
PART_LIMIT = 45 * 1000 * 1000   # bytes per part file: GitHub warns above 50 MB and refuses above 100 MiB


def read_arrays(path):
    """The arrays of shader_data.c, in file order, following the part files it includes."""
    text = path.read_text(encoding='latin-1').replace('\r\n', '\n')
    for part in PART_RE.findall(text):
        text += (path.parent / part).read_text(encoding='latin-1').replace('\r\n', '\n')
    return {m.group(1): bytes(int(x, 16) for x in m.group(3).replace('\n', ' ').replace('\t', ' ').split(',') if x.strip())
            for m in ARRAY_RE.finditer(text)}


def write_parts(data_c, arrays):
    """shader_data.c becomes an index that includes shader_data_<n>.c, each under PART_LIMIT; vk_shaders.cpp still
    includes shader_data.c alone, so the compiled renderer is the same."""
    size = lambda t: len(t) + t.count('\n')   # as written, CRLF
    parts, cur = [], []
    for text in arrays:
        if cur and sum(map(size, cur)) + size(text) > PART_LIMIT:
            parts.append(cur)
            cur = []
        cur.append(text)
    if cur:
        parts.append(cur)
    names = ['shader_data_%d.c' % (i + 1) for i in range(len(parts))]
    for name, part in zip(names, parts):
        (data_c.parent / name).write_bytes(''.join(part).replace('\n', '\r\n').encode('latin-1'))   # the checkout is CRLF
    for stale in data_c.parent.glob('shader_data_*.c'):
        if stale.name not in names:
            stale.unlink()   # a part left over from a larger set
    index = ['// this file is autogenerated during shader compilation (tools/renderer_spirv.py): the arrays are split\n',
             '// into parts under 45 MB so no file reaches the hosting limits\n'] + ['#include "%s"\n' % n for n in names]
    data_c.write_bytes(''.join(index).replace('\n', '\r\n').encode('latin-1'))
    return names


def c_array(var, data):
    parts = []
    for j, b in enumerate(data):
        parts.append('0x%.2X' % b)
        if j + 1 < len(data):
            parts.append(', ' if (j + 1) % 16 else ',\n\t')
    return 'const unsigned char %s[%d] = {\n\t%s\n};\n' % (var, len(data), ''.join(parts))


def instructions(data):
    words = [int.from_bytes(data[i:i + 4], 'little') for i in range(20, len(data), 4)]
    out, i = [], 0
    while i < len(words):
        n = words[i] >> 16
        if n == 0:
            return None
        out.append(tuple(words[i:i + n]))
        i += n
    return out


def same_but_header(a, b):
    """Equal apart from the header's version and generator words and the order of decorations (newer glslang
    emits OpDecorate/OpMemberDecorate in a different order; the module is otherwise the same instruction for
    instruction)."""
    if a is None or b is None or len(a) != len(b) or a[:4] != b[:4] or a[12:20] != b[12:20]:
        return False
    if a[20:] == b[20:]:
        return True
    ia, ib = instructions(a), instructions(b)
    if ia is None or ib is None:
        return False
    deco = lambda ins: (ins[0] & 0xffff) in (71, 72)   # OpDecorate, OpMemberDecorate
    return ([x for x in ia if not deco(x)] == [x for x in ib if not deco(x)]
            and sorted(x for x in ia if deco(x)) == sorted(x for x in ib if deco(x)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['build', 'verify', 'split'])
    ap.add_argument('--tree', default=str(DEFAULT_TREE))
    ap.add_argument('--compiler', choices=['glslang', 'godot'], default='glslang')
    args = ap.parse_args(argv)
    tree = Path(args.tree)
    data_c, bind_c = tree / 'shaders' / 'spirv' / 'shader_data.c', tree / 'shaders' / 'spirv' / 'shader_binding.c'
    committed = read_arrays(data_c)
    if args.cmd == 'split':   # rewrite the committed arrays as parts (e.g. after compile.bat's single file); no compile
        names = write_parts(data_c, [c_array(v, d) for v, d in committed.items()])
        print('wrote %s and %s' % (data_c, ', '.join(names)))
        return 0
    todo, out = compile_all(tree, args.compiler)
    for var in out:
        if out[var] is None:
            if var not in committed:
                raise SystemExit('no committed bytes for %s (geometry shaders are kept on the Godot path)' % var)
            out[var] = committed[var]
    changed = [v for v in out if not same_but_header(out[v], committed.get(v))]
    missing = sorted(set(committed) - set(out))
    print('%d shaders; %d differ from the committed bytes beyond the header%s' % (
        len(out), len(changed), (': ' + ', '.join(changed[:12]) + (' ...' if len(changed) > 12 else '')) if changed else ''))
    if missing:
        print('committed but no longer built: %s' % ', '.join(missing))
    if args.cmd == 'verify':
        return 1 if changed or missing else 0
    seen, text = set(), []
    for name, stage, var, binding, defines in todo:
        if var in seen:
            continue
        seen.add(var)
        keep = var in committed and same_but_header(out[var], committed[var])
        text.append(c_array(var, committed[var] if keep else out[var]))   # unchanged shaders keep their bytes
    write_parts(data_c, text)
    b = ['// this file is autogenerated during shader compilation\n',
         'static void vk_set_shader_name( VkShaderModule shader, const char *name ) {\n',
         '    VK_SET_OBJECT_NAME( shader, name, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );\n', '}\n',
         'void vk_bind_generated_shaders( void ){\n']
    for name, stage, var, binding, defines in todo:
        if binding:
            b.append('    %s = SHADER_MODULE( %s );\n    vk_set_shader_name( %s, "%s" );\n' % (binding, var, binding, var))
    b.append('}')
    bind_c.write_bytes(''.join(b).replace('\n', '\r\n').encode('latin-1'))
    print('wrote %s and %s' % (data_c, bind_c))
    return 0


if __name__ == '__main__':
    sys.exit(main())
