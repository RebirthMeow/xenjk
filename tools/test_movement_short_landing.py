"""Compile the actual straight-proposal gate against the retained bend.

This tests eligibility, not admission or native physics. Generated C, binaries,
logs and a pre-change gate negative control are retained.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import uuid

from test_movement_landing_progress import PRELUDE, function


MAIN = r'''
int main(void){
    bot2_movePath_t path={0};playerState_t ps={0};vec3_t end,aim;
    int i,far;float length;
    path.count=3;
    cells[0].ix=14;cells[0].iy=2;
    cells[1].ix=18;cells[1].iy=15;
    cells[2].ix=16;cells[2].iy=30;
    for(i=0;i<3;i++){cells[i].z=1808.125f;path.cells[i]=i;path.edges[i]=i;edges[i].type=GRAPH_EDGE_STRAFE;}
    ps.origin[0]=562.939f;ps.origin[1]=349.301f;ps.origin[2]=1808.125f;
    far=StraightAhead(&path,0);CHECK(far==1);
    CellPoint(path.cells[far],end);
    length=sqrtf((end[0]-ps.origin[0])*(end[0]-ps.origin[0])+(end[1]-ps.origin[1])*(end[1]-ps.origin[1]));
    CHECK(fabsf(length-131.35f)<.01f);
    CHECK(ProposalGate(far,length,1,5000));
    CHECK(!ProposalGate(far,length,0,5000));
    CHECK(!ProposalGate(-1,length,1,5000));
    CHECK(!ProposalGate(far,length,1,127.99f));
    CHECK(ProposalGate(far,length,1,128));
    CHECK(ProposalGate(far,192,0,5000));
    CHECK(!ProposalGate(far,191.99f,0,5000));
    /* Existing path arc targeting continues through the bend that stopped
       StraightAhead. Eligibility offers a maneuver; it does not prove it. */
    PathPointAt(&path,0,600,aim);CHECK(aim[1]>480);CHECK(aim[0]<576);
    puts("PASS retained short landing, walking/goal/path boundaries and existing arc target");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=shutil.which('gcc') or shutil.which('clang'))
    args = parser.parse_args()
    if not args.cc:
        parser.error('GCC or Clang required')
    root = Path(__file__).resolve().parents[1]
    data = (root / 'codemp/game/bot2_move.c').read_bytes()
    source = data.decode('utf-8')
    gate = re.search(r'^\s*straight=far>=0[^;]+;', source, re.M).group(0).strip()
    gate = gate.replace('GoalDistance(ps,m->objective)', 'goal')
    old = gate.replace(' || landing', '')
    if old == gate:
        raise RuntimeError('Expected landing proposal branch not found')
    constants = '\n'.join(re.search(r'^#define ' + name + r'\s+[^\n]+', source, re.M).group(0)
        for name in ('MOVE_STRAIGHT_CELLS', 'MOVE_STRAIGHT_LATERAL', 'MOVE_STRAIGHT_MIN', 'MOVE_STRAIGHT_GOAL')) + '\n'
    helpers = '\n'.join(function(source, name) for name in ('CellPoint', 'PathStrafe', 'StraightAhead', 'PathPointAt'))
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output = root / '.codex-research' / f'short-landing-test-{stamp}-{uuid.uuid4().hex[:8]}'
    output.mkdir(parents=True, exist_ok=False)
    records = []
    for name, statement, expected in [('production', gate, True), ('pre-change-gate', old, False)]:
        wrapper = '\nstatic int ProposalGate(int far,float length,int landing,float goal){int straight;' + statement + 'return straight;}\n'
        cfile = output / (name + '.c')
        cfile.write_text(PRELUDE + constants + helpers + wrapper + MAIN, encoding='utf-8')
        exe = output / (name + ('.exe' if os.name == 'nt' else ''))
        built = subprocess.run([args.cc, '-std=c99', '-O0', str(cfile), '-lm', '-o', str(exe)], capture_output=True, text=True)
        (output / (name + '.compile.log')).write_text(built.stdout + built.stderr, encoding='utf-8')
        if built.returncode:
            raise RuntimeError(f'Compile failed: {output}')
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        (output / (name + '.run.log')).write_text(run.stdout + run.stderr, encoding='utf-8')
        records.append(dict(name=name, expected_success=expected, exit_code=run.returncode))
        if (run.returncode == 0) != expected:
            raise AssertionError(f'Unexpected result: {output}\n{run.stdout}{run.stderr}')
    (output / 'result.json').write_text(json.dumps(dict(source_sha256=hashlib.sha256(data).hexdigest(),
        compiler=args.cc, tests=records, scope='Extracted proposal gate and path targeting; no physics or admission claims'), indent=2) + '\n', encoding='utf-8')
    print(f'PASS short-landing proposal regression; artifacts: {output}')


if __name__ == '__main__':
    main()
