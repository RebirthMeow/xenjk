"""Configure and build the dedicated movement fixture, without installing it.

Normalizes child environment key casing to avoid MSBuild Path/PATH duplicates.
Uses a new build directory by default; --incremental explicitly reuses one.

**The build's exit code does not depend on the builder's alone.** One rebuild
compiled the dedicated engine, failed the game DLL and reported
success; only the missing hash manifest kept the broken fixture out of a
campaign, and a guard that holds by accident is a trap. So the outputs are
checked directly before the manifest is written: MSBuild's own verdict line in
the log, and the two binaries every world stages. Either one failing is a
non-zero exit and no manifest, which is what stops
`movement_bench.stage_build` from ever seeing the build. What is built is
unchanged.
"""
import argparse
import hashlib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess

# Every world stages exactly these two, by name, from RelWithDebInfo.
BINARIES = ('openjkded.x86.exe', 'jampgamex86.dll')


def missing_binaries(out):
    """The staged binaries this build did not leave behind."""
    return [name for name in BINARIES if not (Path(out) / 'RelWithDebInfo' / name).is_file()]


def build_failed(text):
    """MSBuild's own verdict, read whatever the process exit code was."""
    return any(line.strip() == 'Build FAILED.' for line in text.splitlines())


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--incremental',action='store_true')
    args=ap.parse_args()
    root=Path(__file__).resolve().parent.parent
    out=args.out.resolve()
    out.mkdir(parents=True,exist_ok=args.incremental)
    source_paths=[root/'CMakeLists.txt']
    for directory in ('codemp','shared','lib','cmake'):
        source_paths.extend(p for p in (root/directory).rglob('*') if p.is_file() and
                            (p.suffix.lower() in ('.c','.cpp','.h','.hpp','.cmake') or p.name=='CMakeLists.txt'))
    source_hashes={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths}
    options={key:'OFF' for key in ('BuildMPEngine','BuildMPRdVanilla','BuildMPRend2',
        'BuildMPCGame','BuildMPUI','BuildSPEngine','BuildSPGame','BuildSPRdVanilla')}
    options.update(BuildMPDed='ON',BuildMPGame='ON',BuildPortableVersion='ON',
        CMAKE_C_FLAGS_RELWITHDEBINFO='/O2 /Ob1 /Zi /DNDEBUG',
        CMAKE_CXX_FLAGS_RELWITHDEBINFO='/O2 /Ob1 /Zi /DNDEBUG',
        CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO='/DEBUG /INCREMENTAL:NO',
        CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO='/DEBUG /INCREMENTAL:NO',
        CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO='/DEBUG /INCREMENTAL:NO')
    commands=[['cmake','-S',str(root),'-B',str(out),'-G','Visual Studio 17 2022','-A','Win32']+
              [f'-D{k}={v}' for k,v in options.items()],
              ['cmake','--build',str(out),'--config','RelWithDebInfo','--parallel','4']]
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    log=out/(stamp+'.log')
    with log.open('x') as f:
        for cmd in commands:
            f.write(json.dumps(cmd)+'\n');f.flush()
            r=subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT,
                             env={k.upper():v for k,v in os.environ.items()})
            if r.returncode:
                print('\n'.join(log.read_text(errors='replace').splitlines()[-30:]));return r.returncode
    text=log.read_text(errors='replace')
    if build_failed(text):
        print('\n'.join(text.splitlines()[-30:]))
        print('MSBuild reported Build FAILED; retain this build as invalid:',str(log));return 2
    absent=missing_binaries(out)
    if absent:
        print('\n'.join(text.splitlines()[-30:]))
        print('Build produced no',', '.join(absent),'; retain this build as invalid:',str(log));return 2
    changed=[name for name,digest in source_hashes.items() if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest]
    if changed:
        print('Build sources changed during compilation; retain this build as invalid:',changed);return 1
    (out/(stamp+'.sources.json')).write_text(json.dumps(dict(sources=source_hashes,
        binaries={name:hashlib.sha256((out/'RelWithDebInfo'/name).read_bytes()).hexdigest()
                  for name in BINARIES}),indent=2))
    print(f'Built dedicated engine and game with symbols: {out / "RelWithDebInfo"}')
    print(f'Build log: {log}')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
