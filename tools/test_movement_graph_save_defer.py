"""Compile the deferred graph-cache write and check it writes the file today's autosave writes.

`bot_moveGraphSaveDefer` replaces the full cache rewrite on every learned edge
(`docs/movement/records/2026-09-22-boundary-cost-attribution.md`) with a
snapshot of the edge records and the capped flag, written at shutdown, at map
prepare and from `Graph_Autosave`. The claim that makes it safe to measure is
that the file finally written is byte for byte the one the last autosave would
have written. The writer (`SaveEdges`), the snapshot (`Autosave`,
`Graph_SaveFlush`) and the explicit save (`Graph_Save`) are extracted from
`codemp/game/bot2_graph.c` and compiled against an in-memory file system, so
this is the shipped text and not a transcription of it.

It checks the bookkeeping only. That the saved caches of real worlds are
byte-identical between arms is a native measurement, recorded under
`docs/movement/records/`.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'codemp/game/bot2_graph.c'
HEADER = ROOT / 'codemp/game/bot2_graph.h'
MAIN_C = ROOT / 'codemp/game/g_main.c'
MOVE_C = ROOT / 'codemp/game/bot2_move.c'

PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define CHECK(x) do{if(!(x)){printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
typedef float vec3_t[3];
typedef int qboolean;
#define qtrue 1
#define qfalse 0
typedef int fileHandle_t;
#define FS_WRITE 1
#define MAX_QPATH 64
#define GRAPH_VERSION 4
#define GRAPH_GRID 32
#define STEPSIZE 18
#define GRAPH_CACHE_LINE 320
#define GRAPH_MAX_CELLS 8
#define GRAPH_MAX_NODES 8
#define GRAPH_MAX_EDGES 16
#define Com_sprintf snprintf
static void Q_strcat(char *dest,int size,const char *src){int l=(int)strlen(dest);snprintf(dest+l,size-l,"%s",src);}
static char *va(const char *format,...){static char buffer[256];va_list args;va_start(args,format);vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);return buffer;}
'''

FILESYSTEM = r'''
/* Two files: the cache and its .tmp, which a save writes first and empties last. writes
   counts the cache's writes; tmpFull is the .tmp's length the last time it held a save. */
static char disk[65536];static int diskLength,writes,opened;static const char *deferCvar="";
static char tmpDisk[65536];static int tmpLength,tmpFull;static int stagingTmp;
static char staging[65536];static int stagingLength;
static int FsOpen(const char *path,fileHandle_t *file,int mode){(void)mode;stagingLength=0;*file=1;opened=1;
    stagingTmp=strlen(path)>4 && !strcmp(path+strlen(path)-4,".tmp");return 0;}
static void FsWrite(const void *data,int length,fileHandle_t file){(void)file;memcpy(staging+stagingLength,data,length);stagingLength+=length;}
static void FsClose(fileHandle_t file){(void)file;
    if(stagingTmp) {memcpy(tmpDisk,staging,stagingLength);tmpLength=stagingLength;if(tmpLength)tmpFull=tmpLength;}
    else {memcpy(disk,staging,stagingLength);diskLength=stagingLength;writes++;}
    opened=0;}
static void Print(const char *format,...){(void)format;}
static void CvarString(const char *name,char *buffer,int size){snprintf(buffer,size,"%s",!strcmp(name,"bot_moveGraphSaveDefer")?deferCvar:"");}
static struct {
    int (*FS_Open)(const char*,fileHandle_t*,int);
    void (*FS_Write)(const void*,int,fileHandle_t);
    void (*FS_Close)(fileHandle_t);
    void (*Print)(const char*,...);
    void (*Cvar_VariableStringBuffer)(const char*,char*,int);
} trapTable={FsOpen,FsWrite,FsClose,Print,CvarString},*trap=&trapTable;
static char cacheTarget[MAX_QPATH]="bot2-graph/mp_ctf_kejim_-1.txt";   /* LoadCache sets it in the game */
'''

MAIN = r'''
static int autosave=1;

static void Setup(void){
    memset(&graph,0,sizeof(graph));memset(&saveSnapshot,0,sizeof(saveSnapshot));
    memset(&saveDirty,0,sizeof(saveDirty));saveDirty.all=1;   /* a load's Reset marks every edge */
    strcpy(graph.map,"mp/ctf_kejim");strcpy(graph.checksum,"-1");graph.gravity=800;graph.speed=250;
    graph.count=2;graph.nodeCount=1;graph.links=1;
    graph.cell[0].ix=1;graph.cell[0].z=1.5f;graph.cell[0].link[0]=1;graph.cell[0].link[1]=graph.cell[0].link[2]=graph.cell[0].link[3]=-1;
    graph.cell[1].ix=2;graph.cell[1].z=1.5f;graph.cell[1].link[2]=0;graph.cell[1].link[0]=graph.cell[1].link[1]=graph.cell[1].link[3]=-1;
    graph.node[0].first=0;graph.node[0].count=2;graph.node[0].width=32;
    graph.edgeCount=1;graph.edge[0].type=5;graph.edge[0].from=0;graph.edge[0].to=1;graph.edge[0].costMs=900;graph.edge[0].dir[0]=1;
    writes=0;diskLength=0;tmpLength=0;tmpFull=0;
}

/* One learned edge, as Graph_AddStrafeEdge appends and autosaves it. */
static void Learn(int i){
    graphEdge_t *e=&graph.edge[graph.edgeCount++];
    memset(e,0,sizeof(*e));
    e->type=5;e->from=i&1;e->to=(i+1)&1;e->costMs=500+i;e->takeoff[0]=10.125f*i;e->landing[1]=-3.5f*i;
    e->dir[0]=.6f;e->dir[1]=.8f;e->validated=1;e->tap=i&1;e->entrySpeed=250+i;e->exitSpeed=400+i;e->entryDir[0]=1;e->exitDir[1]=1;
    if(autosave)Autosave();
}

/* A session: learns, a validation that autosaves, and changes no autosave
   writes (a measured cost, a failure) - today's file does not carry those. */
static void Session(void){
    Learn(1);
    graph.edge[0].costMs=875;SaveDirty(0);                      /* Graph_EdgeCost: no write; marks it (BP-7) */
    Learn(2);
    graph.edge[1].validated=0;graph.edge[1].validated=1;SaveDirty(1);if(autosave)Autosave();   /* Graph_EdgeResult's first validation */
    Learn(3);
    graph.edge[2].costMs=77;graph.edge[3].failures=2;graph.edge[3].disabled=1;  /* after the last autosave */
}

int main(void){
    static char today[65536];int todayLength,todayWrites;
    /* Today: every autosave rewrites the file. */
    Setup();deferCvar="0";Session();
    memcpy(today,disk,diskLength);todayLength=diskLength;todayWrites=writes;
    CHECK(todayWrites==4);
    CHECK(Graph_SaveFlush()==qtrue && writes==4);               /* nothing pending: no write */

    /* Deferred, the default (the cvar unset): nothing is
       written until the flush, and the flush writes today's file. */
    Setup();deferCvar="";Session();
    CHECK(writes==0 && saveSnapshot.pending && saveSnapshot.requests==4);
    CHECK(Graph_SaveFlush()==qtrue);
    CHECK(writes==1 && !saveSnapshot.pending);
    CHECK(diskLength==todayLength && !memcmp(disk,today,todayLength));
    CHECK(tmpFull==todayLength && tmpLength==0);                /* the .tmp held the same save, then was emptied */
    CHECK(Graph_SaveFlush()==qtrue && writes==1);               /* a second flush writes nothing */

    /* An explicit save of the live graph supersedes the older snapshot, as
       today's explicit write superseded the autosaves before it. */
    Setup();deferCvar="1";Session();
    CHECK(writes==0 && saveSnapshot.pending);                  /* an explicit 1 defers too */
    Setup();deferCvar="1";Session();
    CHECK(Graph_Save()==qtrue && writes==1 && !saveSnapshot.pending);
    CHECK(Graph_SaveFlush()==qtrue && writes==1);
    CHECK(diskLength!=todayLength || memcmp(disk,today,todayLength));   /* the live graph carries the later cost */
    puts("PASS deferred cache write matches today's last autosave");
    return 0;
}
'''


def definition(source, name):
    """The text of a function definition (not a forward declaration)."""
    match = re.search(r'^(?:static )?[^\n;]*\b' + re.escape(name) + r'\([^\n;]*\)\s*\{', source, re.M)
    if not match:
        raise ValueError(f'Missing production function {name}')
    end = match.end()
    depth = 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[match.start():end]


def typedef(header, name):
    match = re.search(r'^typedef struct \{\n[^{}]*?\n\} ' + re.escape(name) + ';', header, re.M)
    if not match:
        raise ValueError(f'Missing type {name}')
    return match.group(0)


class DeferredSave(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text(encoding='utf-8')

    def test_every_autosave_site_goes_through_the_switch(self):
        """No learn writes the whole cache directly any more; each asks Autosave."""
        self.assertNotIn('if(autosave)Graph_Save()', self.source)
        self.assertNotIn('autosave && !Graph_Save()', self.source)
        self.assertEqual(self.source.count('if(autosave)Autosave();'), 4)
        self.assertEqual(self.source.count('autosave && !Autosave()'), 1)

    def test_the_snapshot_is_flushed_where_the_session_ends(self):
        self.assertIn('qboolean Graph_SaveFlush(void);', HEADER.read_text(encoding='utf-8'))
        shutdown = definition(MAIN_C.read_text(encoding='utf-8'), 'G_ShutdownGame')
        self.assertIn('Graph_SaveFlush();', shutdown)
        prepare = definition(self.source, 'Move_PrepareMap')
        self.assertLess(prepare.index('Graph_SaveFlush();'), prepare.index('Reset();'))
        self.assertIn('Graph_SaveFlush();', definition(self.source, 'Graph_Autosave'))

    def test_the_snapshot_is_flushed_at_each_leg_boundary(self):
        """Default on, with a flush at the objective change so
        a crash loses at most one leg: after Move_Reset (which prints the
        census), before the new objective is taken."""
        plan = definition(MOVE_C.read_text(encoding='utf-8'), 'Move_Plan')
        change = plan[plan.index('Distance(goal,m->objective)>64'):]
        self.assertLess(change.index('Move_Reset(client);'), change.index('Graph_SaveFlush();'))
        self.assertLess(change.index('Graph_SaveFlush();'), change.index('VectorCopy(goal,m->objective)'))
        deferred = definition(self.source, 'SaveDeferred')
        self.assertIn('!value[0]', deferred)

    def test_the_flush_writes_todays_file(self):
        cc = shutil.which('gcc') or shutil.which('clang')
        if not cc:
            self.skipTest('no C compiler')
        header = HEADER.read_text(encoding='utf-8')
        reasons = re.search(r'^static const char \*const graphReasons\[\]=\{.*?\};', self.source, re.M | re.S).group(0)
        graph = r'''
#define GRAPH_REASONS ((int)(sizeof(graphReasons)/sizeof(graphReasons[0])))
static struct {
    int count,nodeCount,edgeCount,seeds,seedRefused,samples,refusals,links,refined,boundary,capped;
    int hints,hintsBound;
    int reason[GRAPH_REASONS];
    char map[64],checksum[64];
    int gravity,speed;
    graphCell_t cell[GRAPH_MAX_CELLS];
    graphNode_t node[GRAPH_MAX_NODES];
    graphEdge_t edge[GRAPH_MAX_EDGES];
} graph;
'''
        snapshot = re.search(r'^static struct \{\n    int pending,edgeCount,capped,requests;.*?\n\} saveSnapshot;',
                             self.source, re.M | re.S).group(0)
        dirty = re.search(r'^#define SAVE_DIRTY_MAX .*?\n\} saveDirty=\{1\};', self.source, re.M | re.S).group(0)
        extracted = '\n'.join([
            typedef(header, 'graphCell_t'), typedef(header, 'graphNode_t'), typedef(header, 'graphEdge_t'),
            reasons, graph, FILESYSTEM, snapshot, dirty,
            definition(self.source, 'SaveDirty'),
            'static qboolean SaveEdges(const graphEdge_t *edges,int edgeCount,int capped);',
            'qboolean Graph_Save(void);',
            definition(self.source, 'SaveDeferred'),
            definition(self.source, 'Autosave'),
            definition(self.source, 'Graph_SaveFlush'),
            definition(self.source, 'WriteLine'),
            definition(self.source, 'CellLine'),
            definition(self.source, 'WriteCache'),
            definition(self.source, 'SaveEdges'),
            definition(self.source, 'Graph_Save'),
        ])
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'defer.c').write_text(PRELUDE + extracted + MAIN, encoding='utf-8')
            exe = tmp / 'defer.exe'
            built = subprocess.run([cc, '-std=c99', '-O0', str(tmp / 'defer.c'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('PASS', run.stdout)


if __name__ == '__main__':
    unittest.main()
