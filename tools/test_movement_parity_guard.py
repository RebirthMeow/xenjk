"""Compile production ParityBuild against deterministic forecast results.

Checks refusal admission and prediction ownership, not native physics. Keeps
source snapshots, generated C, binaries, logs and negative-control evidence.
"""
import argparse
import unittest
import sys
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid


PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define Com_sprintf snprintf
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL case=%s line=%d: %s\n",caseName,__LINE__,#x);exit(1);}}while(0)
typedef struct {int count;} bot2_movePlan_t;
typedef struct {bot2_movePlan_t plan;struct {int stage;} executor;} movement_t;
typedef struct {struct {int number;} s;} gentity_t;
typedef struct {int unused;} pmove_t;
typedef struct {int unused;} bot2_maneuverState_t;
typedef struct {bot2_movePlan_t *plan;} context_t;
typedef struct {int marker;} bot2_forecastFrame_t;
typedef struct {bot2_forecastFrame_t *frames;} bot2_forecastRequest_t;
typedef struct {int status,completion,count;unsigned int unsupported;
    int worldChanged,restored,animationChanged,animationRestored;} bot2_forecastResult_t;
static movement_t movement[2];
static struct {int time;} level;
static struct {context_t context;bot2_forecastResult_t result;
    bot2_forecastFrame_t frames[8],built[8];int rollouts,refusals,client,armed,index,count;
    int completion,builtTime,truncated,planned;} parity;
static bot2_forecastResult_t supplied;
static char lastDetail[256],lastEvent[40];
static const char *caseName;
static int forecastCalls;
static void RolloutRequest(gentity_t *e,pmove_t *m,context_t *c,
    bot2_maneuverState_t *p,int horizon,bot2_forecastRequest_t *r){r->frames=parity.frames;}
static void Bot2_ForecastRun(bot2_forecastRequest_t *r,bot2_forecastResult_t *out){
    CHECK(r->frames==parity.built);r->frames[0].marker=1234;
    *out=supplied;forecastCalls++;
}
/* bot2_move.c wraps the run in a world-guard scope; the guard is not part of this harness. */
static int ScopedForecastRun(const pmove_t *move,bot2_forecastRequest_t *r,bot2_forecastResult_t *out){
    (void)move;Bot2_ForecastRun(r,out);return 1;
}
static int Onward(const bot2_movePlan_t *p){return 3;}
static void ParityEvent(int client,const char *event,const char *detail){
    snprintf(lastEvent,sizeof(lastEvent),"%s",event);
    snprintf(lastDetail,sizeof(lastDetail),"%s",detail);
}
'''

MAIN = r'''
static int cases;
static void runCase(const char *name,int accept,int truncated){
    gentity_t ent={{1}};pmove_t move={0};caseName=name;
    memset(&parity,0,sizeof(parity));
    parity.armed=1;parity.index=7;parity.count=7;parity.truncated=1;
    parity.frames[0].marker=777;forecastCalls=0;lastEvent[0]=lastDetail[0]=0;
    level.time=12345;movement[1].plan.count=2;movement[1].executor.stage=1;
    ParityBuild(&ent,&move,8);
    CHECK(forecastCalls==1);CHECK(parity.rollouts==1);
    CHECK(parity.frames[0].marker==777);CHECK(parity.built[0].marker==1234);
    CHECK(parity.context.plan==&movement[1].plan);
    CHECK(parity.armed==accept);CHECK(parity.index==0);
    CHECK(parity.refusals==!accept);
    if(accept){
        CHECK(parity.count==supplied.count);CHECK(parity.truncated==truncated);
        CHECK(parity.client==1);CHECK(parity.builtTime==level.time);
        CHECK(parity.completion==supplied.completion);CHECK(parity.planned==1);
        CHECK(!lastEvent[0]);
    }else{
        CHECK(parity.count==0);CHECK(parity.truncated==0);
        CHECK(!strcmp(lastEvent,"forecast_refused"));
    }
    cases++;printf("PASS %s accepted=%d\n",name,accept);
}
static void cleanResult(int trigger){
    memset(&supplied,0,sizeof(supplied));supplied.count=4;
    supplied.restored=supplied.animationRestored=1;
    supplied.status=trigger?BOT2_FORECAST_REFUSED:BOT2_FORECAST_COMPLETE;
    supplied.unsupported=trigger?BOT2_FORECAST_TRIGGER:0;
    supplied.completion=2;
}
int main(void){
    int trigger,bit,status;char name[100];
    cleanResult(0);runCase("complete",1,0);
    cleanResult(1);runCase("trigger_only",1,1);
    /* The original implementation admits this unsupported mutation even
       when the forecast correctly reports REFUSED and restores animation. */
    cleanResult(1);supplied.animationChanged=1;
    runCase("trigger_plus_animation_change",0,0);
    CHECK(strstr(lastDetail,"animation_changed=1")!=NULL);
    for(trigger=0;trigger<2;trigger++){
        for(bit=0;bit<=10;bit++){
            if(bit==8)continue;
            cleanResult(trigger);supplied.status=BOT2_FORECAST_REFUSED;
            supplied.unsupported|=1u<<bit;
            snprintf(name,sizeof(name),"trigger_%d_unsupported_bit_%d",trigger,bit);
            runCase(name,0,0);
        }
        for(bit=0;bit<6;bit++){
            cleanResult(trigger);
            if(bit==0)supplied.worldChanged=1;
            if(bit==1)supplied.restored=0;
            if(bit==2)supplied.animationChanged=1;
            if(bit==3)supplied.animationRestored=0;
            if(bit==4)supplied.count=0;
            if(bit==5)supplied.count=-1;
            snprintf(name,sizeof(name),"trigger_%d_contract_failure_%d",trigger,bit);
            runCase(name,0,0);
        }
        for(status=BOT2_FORECAST_BAD_ARGUMENT;status<=BOT2_FORECAST_BUSY;status++){
            cleanResult(trigger);supplied.status=status;
            snprintf(name,sizeof(name),"trigger_%d_status_%d",trigger,status);
            runCase(name,0,0);
        }
    }
    cleanResult(0);supplied.status=BOT2_FORECAST_REFUSED;runCase("refused_without_trigger",0,0);
    cleanResult(1);supplied.status=BOT2_FORECAST_COMPLETE;runCase("complete_with_unsupported_trigger",0,0);
    printf("PASS %d cases\n",cases);return 0;
}
'''


def extract(source, name):
    start = source.index('static void ' + name + '(')
    opening = source.index('{', start)
    # These two functions have no braces in literals or comments.
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=shutil.which('gcc') or shutil.which('clang'))
    parser.add_argument('--negative-source', type=Path, help='retained original bot2_move.c')
    args = parser.parse_args()
    if not args.cc:
        parser.error('GCC or Clang is required')
    root = Path(__file__).resolve().parents[1]
    source_bytes = (root / 'codemp/game/bot2_move.c').read_bytes()
    source = source_bytes.decode('utf-8').replace('\r\n', '\n')
    header_bytes = (root / 'codemp/game/bot2_forecast.h').read_bytes()
    header = header_bytes.decode('utf-8').replace('\r\n', '\n')
    start = header.index('typedef enum {\n    BOT2_FORECAST_COMPLETE')
    end = header.index('\n};', start) + len('\n};')
    enums = header[start:end]
    function = extract(source, 'ParityBuild')
    if args.negative_source:
        negative_bytes = args.negative_source.read_bytes()
        negative = extract(negative_bytes.decode('utf-8'), 'ParityBuild')
    else:
        guard = ' || parity.result.animationChanged'
        if function.count(guard) != 1:
            raise AssertionError('Expected exactly one animation-change guard')
        negative = function.replace(guard, '', 1)
        negative_bytes = negative.encode('utf-8')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output = root / '.codex-research' / f'movement-parity-guard-test-{stamp}-{uuid.uuid4().hex[:8]}'
    output.mkdir(parents=True, exist_ok=False)
    (output / 'bot2_move.c').write_bytes(source_bytes)
    (output / 'bot2_forecast.h').write_bytes(header_bytes)
    (output / 'negative-source.c').write_bytes(negative_bytes)
    records = []
    for name, body, success in [('production', function, True), ('missing-animation-guard', negative, False)]:
        cfile = output / (name + '.c')
        exe = output / (name + ('.exe' if os.name == 'nt' else ''))
        cfile.write_text(PRELUDE + enums + '\n' + extract(source, 'ParityDisarm') + body + MAIN, encoding='utf-8')
        built = subprocess.run([args.cc, '-std=c99', '-O0', str(cfile), '-o', str(exe)], capture_output=True, text=True)
        (output / (name + '.compile.log')).write_text(built.stdout + built.stderr, encoding='utf-8')
        if built.returncode:
            raise RuntimeError(f'Compile failed: {output}')
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        (output / (name + '.run.log')).write_text(run.stdout + run.stderr, encoding='utf-8')
        records.append(dict(name=name, expected_success=success, exit_code=run.returncode))
        if (run.returncode == 0) != success:
            raise AssertionError(f'Unexpected {name} result: {output}\n{run.stdout}{run.stderr}')
        if not success and 'case=trigger_plus_animation_change' not in run.stderr:
            raise AssertionError(f'Negative control failed for the wrong reason: {run.stderr}')
    (output / 'result.json').write_text(json.dumps(dict(
        source_sha256=hashlib.sha256(source_bytes).hexdigest(),
        forecast_header_sha256=hashlib.sha256(header_bytes).hexdigest(),
        negative_source_sha256=hashlib.sha256(negative_bytes).hexdigest(),
        negative_source=str(args.negative_source) if args.negative_source else 'guard-removal mutation',
        compiler=args.cc, scope='Actual ParityBuild/ParityDisarm; deterministic forecast boundary',
        tests=records), indent=2) + '\n', encoding='utf-8')
    print(f'PASS parity refusal guards; artifacts: {output}')


class ScriptTest(unittest.TestCase):
    """The script above as a suite case: it extracts production code, compiles it with stubs and runs it."""

    @unittest.skipUnless(shutil.which(os.environ.get('CC') or 'gcc') or shutil.which('clang'), 'no C compiler')
    def test_script(self):
        run = subprocess.run([sys.executable, __file__], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    main()
