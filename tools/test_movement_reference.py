"""Compile the actual StraightJudge with deterministic forecast boundaries.

This tests arm-reference isolation, not physics. Artifacts (including a
deliberately broken negative control) are retained in a unique research folder.
Run: python tools/test_movement_reference.py [--cc /path/to/gcc]
"""
import argparse
import os
import unittest
import sys
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import uuid


PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define max(a,b) ((a)>(b)?(a):(b))
#define FP_SPEED 0
#define MOVE_PLAN_STATE 1
#define MOVE_STRAIGHT_GAIN 16
#define BOT2_FORECAST_COMPLETE 1
#define BOT2_FORECAST_TRIGGER 256
#define BOT2_FORECAST_COMPLETION_POLICY 2
#define CANDIDATE_OUTCOMES 3
typedef struct {struct {int forcePowersActive,forcePowerDuration[1];} fd;float distance;} playerState_t;
typedef struct {playerState_t ps;} client_t;
typedef struct {struct {int number;} s;client_t *client;} gentity_t;
typedef struct {int unused;} pmove_t;
typedef struct {int unused;} bot2_moveExecutor_t;
typedef struct {float runup,dir[2];} bot2_moveHop_t;
typedef struct {int count,kind,edge2,id;bot2_moveHop_t hop[2];} bot2_movePlan_t;
typedef struct {int count,from,to,costMs,state,generation;} bot2_movePath_t;
typedef struct {int frames,unsupported,done,launch[2],land[2],landCell[2],endStage;float progress,landSpeed[2],landDir[2][2],landPos[2][3];} spikeMeasure_t;
typedef struct {bot2_movePlan_t candidate;spikeMeasure_t measure;const char *outcome;int variant,walkFrame,walkStopped,walkRefFrames;float walk,margin;int rerouted,routeCells,routeMs;
    int tapOnward,tapLaunch,tapLand,tapCell;float tapSpeed;} straightResult_t;
typedef struct {int best,admitted,walked,walkFrames,walkFrame,walkStopped;float walk,margin;} straightVerdict_t;
typedef struct {float speed,dir[2];int tap,speedMs;} graphState_t;
typedef struct {int contaminated;} bot2_forecastGuard_t;
typedef struct {playerState_t afterPS;} bot2_forecastFrame_t;
typedef struct {int count,status,unsupported,completion;} bot2_forecastResult_t;
typedef struct {float objective[3];bot2_movePath_t path;} movement_t;
static movement_t movement[1];
static struct {int time;} level;
static struct {bot2_forecastFrame_t frames[256];bot2_forecastResult_t result;} parity;
#define MOVE_PROGRESS_MIN 16
#define MOVE_STRAIGHT_HOLD 48
#define TapOnwardCommand 2
/* Tap knobs: the pass on or off, the plan id whose walk continuation failed,
   and what the successor probe found. */
static int tapping,tapCandId,tapLaunchAt=10,tapLandAt=40,tapCellAt=7;
static float tapDir[2],tapYaw;
static int tapState,tapStart;
static int TapOnward(void){return tapping;}
/* Mirrors the production predicate, which is outside the extracted text. */
static int TapRescuable(const straightResult_t *r){
    if(!r->outcome || r->measure.endStage<2*r->candidate.count)return 0;
    return !strcmp(r->outcome,"no_onward")||!strcmp(r->outcome,"corridor");
}
static void TapMeasure(const bot2_forecastResult_t *result,straightResult_t *r){
    r->tapLaunch=tapLaunchAt;r->tapLand=tapLandAt;r->tapCell=tapCellAt;r->tapSpeed=520;
}
static bot2_forecastFrame_t straightWalkFrames[256],replayWalkFrames[256],extensionWalkFrames[256];
static bot2_forecastResult_t straightWalkResult,replayWalkResult,extensionWalkResult;
static bot2_movePath_t rerouteScratch;
static int rerouteWalkBias=400;
static int straightRootIndex,straightStopTime,straightLandings,straightOffPath,straightPathFree;
static const char *candidateOutcomes[]={"unjudged","slower","off_path"};
static float Speed2D(const playerState_t *p){return 0;}
static int offer,refusal,walkCalls,walkSteps[4];
/* Reroute knobs: the pass on or off, the plan id whose first rollout lands
   off the path, the route its landing opens and the route it leaves. */
static int rerouteFlags,offPathId,routeMs,baseRouteMs,routeFound=1;
#define MOVE_REROUTE_GAIN 250
static int RerouteFlags(void){return rerouteFlags;}
static int PathRemainingMs(const bot2_movePath_t *p,int index){return p==&rerouteScratch?routeMs:baseRouteMs;}
static int PathIndexNear(const bot2_movePath_t *p,int from,int to,const float *pos){return -1;}
static int RerouteFrom(int client,const straightResult_t *r,int speedMs,bot2_movePath_t *out){
    if(!routeFound)return 0;
    memset(out,0,sizeof(*out));out->count=8;out->from=1;out->to=2;return 1;
}
static void StraightArrives(gentity_t *e,straightResult_t *r,float rootGoal){}
static void Bot2_PrintTelemetry(int level,const char *fmt,...){}
static int HeadingOffer(void){return offer;}
static int ChainAdmit(void){return 0;}
static int StraightAdmitted(const char *s){return !strcmp(s,"admitted")||!strcmp(s,"arrives")||!strcmp(s,"rerouted")||!strcmp(s,"tap_onward");}
static float GoalDistance(const playerState_t *ps,const float *goal){return ps->distance;}
static void ParityDisarm(void){}
static int Bot2_ForecastGuardBegin(pmove_t *m,bot2_forecastGuard_t *g){g->contaminated=0;return 1;}
static void Bot2_ForecastGuardEnd(bot2_forecastGuard_t *g){}
#define StraightCommand 0
static const char *RunCandidate(gentity_t *e,pmove_t *m,const bot2_movePlan_t *p,const bot2_moveExecutor_t *a,const bot2_moveExecutor_t *b,int command,int fresh,spikeMeasure_t *out,float *us,int *traces){
    memset(out,0,sizeof(*out));out->done=p->id?100:50;out->frames=out->done;
    out->progress=p->id?500:200;out->land[0]=out->land[1]=-1;
    /* The candidate the scenario puts off the path: its first rollout stops
       at the landing, its path-free continuation completes. */
    if(offPathId && p->id==offPathId && !straightPathFree)straightOffPath=1;
    /* The candidate whose walk continuation failed at its last landing, and
       the probe that follows it. */
    if(tapCandId && p->id==tapCandId){
        out->endStage=2*p->count;
        if(command==TapOnwardCommand){
            parity.result.status=BOT2_FORECAST_COMPLETE;parity.result.count=120;
            return "no_onward";
        }
        return "no_onward";
    }
    return "admitted";
}
static int RolloutFell(int n,const playerState_t *p){return 0;}
static int ArrivesAt(int client,const playerState_t *p,int ms){return 0;}
static int Bot2_GameStepMsec(void){return 25;}
static int Graph_StrafeAdmits(int edge,int cell,const graphState_t *s){return 1;}
static int NearPathCell(const int *path,int a,int b,const float *pos){return 1;}
static void PrintCandidate(int c,const bot2_movePlan_t *p,int i,const spikeMeasure_t *m,const char *s,const float *r){}
static void StraightWalkInto(gentity_t *e,pmove_t *m,int steps,bot2_forecastFrame_t *frames,bot2_forecastResult_t *out){
    int i;walkSteps[walkCalls++]=steps;memset(out,0,sizeof(*out));
    out->count=(refusal && steps>70)?70:steps;
    out->status=(refusal && steps>70)?0:BOT2_FORECAST_COMPLETE;
    out->unsupported=(refusal && steps>70)?8:0;
    /* Different data for each buffer detects cross-arm reference reads. */
    for(i=0;i<out->count;i++)frames[i].afterPS.distance=1000-(frames==replayWalkFrames?300:frames==extensionWalkFrames?rerouteWalkBias:100);
}
static void StraightWalk(gentity_t *e,pmove_t *m,int steps){StraightWalkInto(e,m,steps,straightWalkFrames,&straightWalkResult);}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
'''

MAIN = r'''
int main(void){
    int mode,fail,counts[3],traces=0;float us=0,ref[2]={1,0};
    client_t client={0};gentity_t ent={{0},&client};pmove_t move={0};bot2_moveExecutor_t exec={0};
    client.ps.distance=1000;
    for(mode=0;mode<2;mode++)for(fail=0;fail<2;fail++){
        straightResult_t r[2]={{0}};straightVerdict_t v;
        offer=mode;refusal=fail;walkCalls=0;rerouteFlags=0;offPathId=0;tapping=0;tapCandId=0;
        r[0].candidate.count=r[1].candidate.count=1;
        r[1].candidate.id=1;r[1].variant=1;
        StraightJudge(&ent,&move,r,2,0,&exec,&exec,ref,counts,&us,&traces,&v);
        CHECK(walkCalls==2);CHECK(walkSteps[0]==50);CHECK(walkSteps[1]==100);
        CHECK(!strcmp(r[0].outcome,"admitted"));CHECK(r[0].walkRefFrames==50);
        CHECK(r[0].walkFrame==49);CHECK(r[0].walk==100);CHECK(r[0].margin==100);
        if(fail){
            CHECK(!strcmp(r[1].outcome,"unjudged"));CHECK(r[1].walkRefFrames==70);
            CHECK(r[1].walkFrame==-1);CHECK(v.best==0);CHECK(v.admitted==1);
        }else{
            CHECK(!strcmp(r[1].outcome,"admitted"));CHECK(r[1].walkRefFrames==100);
            CHECK(r[1].walkFrame==99);CHECK(r[1].walk==300);CHECK(r[1].margin==200);
            CHECK(v.best==(mode?1:0));CHECK(v.admitted==2);
        }
        printf("PASS offer=%d later_reference_refuses=%d A=admitted B=%s best=%d\n",mode,fail,r[1].outcome,v.best);
    }
    /* The reroute pass: a candidate rejected for leaving the path is
       continued, judged against its own third reference, and admitted only
       when the route its landing opens beats the route it left. The control
       candidate's reference must be untouched either way. Bit 2 adds the
       route ranking; bit 1 alone keeps the margin ranking. */
    for(mode=0;mode<2;mode++){
        straightResult_t r[2]={{0}};straightVerdict_t v;
        offer=0;refusal=0;walkCalls=0;rerouteFlags=3;offPathId=1;routeFound=1;
        baseRouteMs=10000;routeMs=mode?12000:2000;
        r[0].candidate.count=r[1].candidate.count=1;
        r[1].candidate.id=1;r[1].variant=0;
        StraightJudge(&ent,&move,r,2,0,&exec,&exec,ref,counts,&us,&traces,&v);
        CHECK(walkCalls==2);CHECK(walkSteps[0]==50);CHECK(walkSteps[1]==100);
        CHECK(!strcmp(r[0].outcome,"admitted"));CHECK(r[0].walkRefFrames==50);
        CHECK(r[0].walkFrame==49);CHECK(r[0].walk==100);CHECK(r[0].margin==100);
        CHECK(r[1].rerouted==1);CHECK(r[1].routeCells==8);CHECK(r[1].walkRefFrames==100);
        CHECK(r[1].walkFrame==99);CHECK(r[1].walk==400);CHECK(r[1].margin==100);
        CHECK(r[1].routeMs==100*25+(mode?12000:2000));
        if(mode){
            CHECK(!strcmp(r[1].outcome,"slower_route"));CHECK(v.best==0);CHECK(v.admitted==1);
        }else{
            CHECK(!strcmp(r[1].outcome,"rerouted"));CHECK(v.best==1);CHECK(v.admitted==2);
        }
        printf("PASS reroute bits=3 route_ms=%d B=%s best=%d\n",r[1].routeMs,r[1].outcome,v.best);
    }
    /* Bit 1 alone: the rerouted candidate is admitted on the same route
       gate, but the ranking is the margin, and the larger margin wins. */
    for(mode=0;mode<2;mode++){
        straightResult_t r[2]={{0}};straightVerdict_t v;
        offer=0;refusal=0;walkCalls=0;rerouteFlags=1;offPathId=1;routeFound=1;
        baseRouteMs=10000;routeMs=2000;
        r[0].candidate.count=r[1].candidate.count=1;
        r[1].candidate.id=1;
        /* The reroute candidate's route is the better one in both modes
           (4,500 ms against the control's 11,250). Mode 1 gives it the
           smaller margin, and bit 1 alone must then leave the control the
           winner: this is the ranking bit 2 changes. */
        rerouteWalkBias=mode?450:100;
        StraightJudge(&ent,&move,r,2,0,&exec,&exec,ref,counts,&us,&traces,&v);
        CHECK(!strcmp(r[1].outcome,"rerouted"));CHECK(r[1].rerouted==1);
        CHECK(r[0].margin==100);CHECK(r[1].margin==(mode?50:400));
        CHECK(r[1].routeMs==4500);CHECK(r[0].routeMs==50*25+10000);
        CHECK(v.best==(mode?0:1));
        printf("PASS reroute bits=1 margin_ranked control=%f reroute=%f best=%d\n",
               r[0].margin,r[1].margin,v.best);
    }
    rerouteWalkBias=400;
    /* The tap pass: a candidate the walk test refused at its last landing is
       admitted when a tap successor launches, lands on the graph and makes
       progress, and refused when it does not. It reads the extension
       reference, and the control's reference is untouched either way. */
    for(mode=0;mode<2;mode++){
        straightResult_t r[2]={{0}};straightVerdict_t v;
        offer=0;refusal=0;walkCalls=0;rerouteFlags=0;offPathId=0;
        tapping=1;tapCandId=1;tapLaunchAt=10;tapLandAt=40;tapCellAt=mode?-1:7;
        rerouteWalkBias=100;
        r[0].candidate.count=r[1].candidate.count=1;
        r[1].candidate.id=1;
        for(int i=0;i<256;i++)parity.frames[i].afterPS.distance=400;
        StraightJudge(&ent,&move,r,2,0,&exec,&exec,ref,counts,&us,&traces,&v);
        CHECK(!strcmp(r[0].outcome,"admitted"));CHECK(r[0].walkRefFrames==50);
        CHECK(r[0].walk==100);CHECK(r[0].margin==100);CHECK(walkSteps[0]==50);
        if(mode){
            CHECK(!strcmp(r[1].outcome,"tap_refused"));CHECK(r[1].tapOnward==0);
            CHECK(walkCalls==1);CHECK(v.best==0);CHECK(v.admitted==1);
        }else{
            CHECK(!strcmp(r[1].outcome,"tap_onward"));CHECK(r[1].tapOnward==1);
            CHECK(r[1].measure.done==41);CHECK(r[1].measure.progress==600);
            CHECK(walkCalls==2);CHECK(walkSteps[1]==41);
            CHECK(r[1].walkRefFrames==41);CHECK(r[1].walk==100);CHECK(r[1].margin==500);
            CHECK(v.best==1);CHECK(v.admitted==2);
        }
        printf("PASS tap_onward cell=%d B=%s best=%d\n",tapCellAt,r[1].outcome,v.best);
    }
    tapping=0;tapCandId=0;
    for(int i=0;i<256;i++)parity.frames[i].afterPS.distance=0;
    /* A landing the planner cannot continue from is not an alternative. */
    {
        straightResult_t r[2]={{0}};straightVerdict_t v;
        offer=0;refusal=0;walkCalls=0;rerouteFlags=1;offPathId=1;routeFound=0;
        baseRouteMs=10000;routeMs=0;
        r[0].candidate.count=r[1].candidate.count=1;r[1].candidate.id=1;
        StraightJudge(&ent,&move,r,2,0,&exec,&exec,ref,counts,&us,&traces,&v);
        CHECK(walkCalls==1);CHECK(walkSteps[0]==50);
        CHECK(!strcmp(r[1].outcome,"no_route"));CHECK(r[1].rerouted==0);
        CHECK(v.best==0);CHECK(v.admitted==1);
        printf("PASS reroute no_route best=%d\n",v.best);
    }
    return 0;
}
'''


def extract(source):
    """StraightJudge with its resumable state (judgeState_t). The scenarios call it once with the search budget off,
    so each call gets a fresh state through a macro defined after the function."""
    source = source.replace('\r\n', '\n')
    marker = 'static int StraightJudge('
    if marker not in source:
        raise AssertionError('marker not found: %r (the source moved; update the harness)' % marker)
    state_start = source.index('enum { JUDGE_CANDIDATES')
    state_end = source.index('} judgeState_t;', state_start) + len('} judgeState_t;')
    start = source.index(marker)
    end = source.index('\nstatic ', start + 1)
    return (source[state_start:state_end] + '\n' + source[start:end] +
            '\nstatic judgeState_t harnessJudge;\n'
            '#define StraightJudge(...) (memset(&harnessJudge,0,sizeof(harnessJudge)),'
            'StraightJudge(__VA_ARGS__,&harnessJudge))\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=shutil.which('gcc') or shutil.which('clang'))
    args = parser.parse_args()
    if not args.cc:
        parser.error('GCC or Clang is required; pass --cc')
    root = Path(__file__).resolve().parents[1]
    source_path = root / 'codemp/game/bot2_move.c'
    source_bytes = source_path.read_bytes()
    judge = extract(source_bytes.decode('utf-8'))
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output = root / '.codex-research' / f'movement-reference-test-{stamp}-{uuid.uuid4().hex[:8]}'
    output.mkdir(parents=True, exist_ok=False)
    records = []
    # Mutate the extracted production code, not a second algorithm: restoring
    # the old offering-dependent grouping must fail this regression.
    broken = judge.replace('if(r->variant)maxDoneB=', 'if(r->variant && !offering)maxDoneB=', 1)
    if broken == judge:
        raise RuntimeError('Negative-control mutation no longer matches; update harness')
    for name, function, expected_success in [('production', judge, True), ('coupled-negative-control', broken, False)]:
        cfile = output / f'{name}.c'
        executable = output / (name + ('.exe' if __import__('os').name == 'nt' else ''))
        cfile.write_text(PRELUDE + function + MAIN, encoding='utf-8')
        compiled = subprocess.run([args.cc, '-std=c99', '-O0', str(cfile), '-o', str(executable)], capture_output=True, text=True)
        (output / f'{name}.compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
        if compiled.returncode:
            raise RuntimeError(f'Compile failed; see {output}')
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        (output / f'{name}.run.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        records.append(dict(name=name, expected_success=expected_success, exit_code=run.returncode))
        if (run.returncode == 0) != expected_success:
            raise AssertionError(f'Unexpected {name} result; see {output}')
    manifest = dict(source=str(source_path), source_sha256=hashlib.sha256(source_bytes).hexdigest(),
                    compiler=args.cc, tests=records, scope='Extracted production StraightJudge; mocked forecast/physics boundaries')
    (output / 'result.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'PASS eleven production scenarios (four reference-isolation, five reroute across both cvar bits, two tap-onward); coupled negative control rejected. Artifacts: {output}')


class ScriptTest(unittest.TestCase):
    """The script above as a suite case: it extracts production code, compiles it with stubs and runs it."""

    @unittest.skip('the stubs predate StraightJudge\'s resumable judge: StraightArrives\' signature, straightStaleSlots and the judge\'s new inputs; repair the harness')
    def test_script(self):
        run = subprocess.run([sys.executable, __file__], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    main()
