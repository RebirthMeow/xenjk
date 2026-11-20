"""Compile the route tables' change fold and check it against a table built afresh.

A route table (`Graph_PathTable`, bot_movePathTables) folds the edge changes
queued since it was built - edges added, made cheaper or dearer, turned off or
back on - instead of rebuilding. `PathFold`, the reversed-arc builder, the heap
and the settle are extracted from `codemp/game/bot2_graph.c` and compiled, so
this is the shipped text. Each trial builds a table on a small grid with edges,
queues a random batch of changes the way the graph's mutators queue them, folds
it, and requires every cell's time to equal a table built from scratch on the
changed graph. Folding one change at a time against the final
costs left stale times (two dearer edges in a chain extracted no route).
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'codemp/game/bot2_graph.c'

PRELUDE = r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#define GRAPH_MAX_CELLS 64
#define GRAPH_MAX_EDGES 96
#define GRAPH_WALK_MS 128
#define GRAPH_DIAGONAL_MS 181
#define PATH_TABLES 2
typedef struct {int link[4],firstEdge;} graphCell_t;
typedef struct {int from,to,costMs,nextFrom,disabled,offUntil;} graphEdge_t;
static struct {
    int count,links,edgeCount;
    graphCell_t cell[GRAPH_MAX_CELLS];
    graphEdge_t edge[GRAPH_MAX_EDGES];
} graph;
static struct {int time;} level;
static unsigned int guideTopo;
static int bot2_meterExpanded,bot2_meterExpandedCell,bot2_meterInReplan,bot2_meterExpandedCellOther;
static int stands[GRAPH_MAX_CELLS],penalty[GRAPH_MAX_CELLS];
static int Graph_CellStands(int c) {return stands[c];}
static float ClearancePenalty(int c) {return (float)penalty[c];}
static int Graph_EdgeOff(const graphEdge_t *e) {return e->disabled || e->offUntil>level.time;}
'''

MAIN = r'''
#define SIDE 6
static unsigned int seed=12345;
static int Rand(int n) {seed=seed*1103515245u+12345u;return (int)((seed>>8)%(unsigned int)n);}
static float table[GRAPH_MAX_CELLS],fresh[GRAPH_MAX_CELLS];

static void Fresh(float *dist,int goal) {
    int i;
    for(i=0;i<graph.count;i++)dist[i]=PATH_UNREACHED;
    dist[goal]=0;pathWork.count=0;PathHeapPush(&pathWork,goal,0);
    PathSettle(dist,&pathWork,0);
}
static void Add(int from,int to,int cost,int queue) {
    graphEdge_t *e=&graph.edge[graph.edgeCount];memset(e,0,sizeof(*e));
    e->from=from;e->to=to;e->costMs=cost;
    e->nextFrom=graph.cell[from].firstEdge;graph.cell[from].firstEdge=graph.edgeCount;
    if(queue)PathChange(graph.edgeCount,-1,0);
    graph.edgeCount++;
}
/* The mutators' queueing, as Graph_EdgeCost, Graph_EdgeFail, OffExpire and AddEdgeRecord do it. */
static void Cost(int e,int cost) {
    if(cost<graph.edge[e].costMs)PathChange(e,graph.edge[e].costMs,0);
    else if(cost>graph.edge[e].costMs)PathChange(e,graph.edge[e].costMs,1);
    graph.edge[e].costMs=cost;
}
static void Off(int e) {if(!Graph_EdgeOff(&graph.edge[e])) {PathChange(e,graph.edge[e].costMs,1);graph.edge[e].offUntil=level.time+1000;}}
static void Back(int e) {if(graph.edge[e].offUntil) {graph.edge[e].offUntil=0;PathChange(e,-1,0);}}

static int Grid(void) {
    int x,y,c;
    graph.count=SIDE*SIDE;graph.edgeCount=0;graph.links++;guideTopo++;
    for(c=0;c<graph.count;c++) {
        x=c%SIDE;y=c/SIDE;
        graph.cell[c].firstEdge=-1;
        graph.cell[c].link[0]=x+1<SIDE?c+1:-1;graph.cell[c].link[1]=y+1<SIDE?c+SIDE:-1;
        graph.cell[c].link[2]=x>0?c-1:-1;graph.cell[c].link[3]=y>0?c-SIDE:-1;
        stands[c]=Rand(10)!=0;penalty[c]=Rand(4)==0?Rand(300):0;
    }
    return Rand(graph.count);
}

static int Compare(int trial,const char *what) {
    int i;
    for(i=0;i<graph.count;i++) {
        int bad=(table[i]>=PATH_UNREACHED)!=(fresh[i]>=PATH_UNREACHED) ||
                (fresh[i]<PATH_UNREACHED && fabsf(table[i]-fresh[i])>.01f);
        if(bad) {printf("FAIL trial %d (%s) cell %d folded %g fresh %g\n",trial,what,i,table[i],fresh[i]);return 1;}
    }
    return 0;
}

int main(void) {
    int trial,i;
    /* A chain 2 -> 1 -> 0 of edges, both made dearer before the table is asked again. */
    graph.count=4;graph.links=1;guideTopo=1;
    for(i=0;i<4;i++) {graph.cell[i].link[0]=graph.cell[i].link[1]=graph.cell[i].link[2]=graph.cell[i].link[3]=-1;
                      graph.cell[i].firstEdge=-1;stands[i]=1;penalty[i]=0;}
    graph.edgeCount=0;Add(1,0,1,0);Add(2,1,1,0);Add(3,0,20,0);Add(2,3,0+1,0);Add(1,3,1,0);
    PathRevBuild();Fresh(table,0);
    {unsigned int since=pathDecreaseSeq;
     Cost(0,50);Cost(1,50);
     PathRevBuild();PathFold(table,since);Fresh(fresh,0);
     if(Compare(-1,"chain"))return 1;}
    /* An edge made cheaper, then dearer than it was, before the table is asked. */
    {unsigned int since;
     Fresh(table,0);since=pathDecreaseSeq;
     Cost(1,1);Cost(0,0+1);Cost(0,90);
     PathRevBuild();PathFold(table,since);Fresh(fresh,0);
     if(Compare(-2,"down-up"))return 1;}

    for(trial=0;trial<20000;trial++) {
        int goal=Grid(),edges=4+Rand(40),changes=1+Rand(8),k;unsigned int since;
        for(i=0;i<edges && graph.edgeCount<GRAPH_MAX_EDGES-16;i++) {
            int a=Rand(graph.count),b=Rand(graph.count);
            if(a!=b)Add(a,b,1+Rand(1200),0);
        }
        for(i=0;i<graph.edgeCount;i++)if(Rand(8)==0)graph.edge[i].offUntil=level.time+1000;
        PathRevBuild();Fresh(table,goal);
        since=pathDecreaseSeq;
        for(k=0;k<changes;k++) {
            int kind=Rand(5),e=graph.edgeCount?Rand(graph.edgeCount):-1;
            if(kind==0 || e<0) {int a=Rand(graph.count),b=Rand(graph.count);if(a!=b)Add(a,b,1+Rand(1200),1);}
            else if(kind==1)Cost(e,1+Rand(1200));
            else if(kind==2)Cost(e,graph.edge[e].costMs+1+Rand(800));
            else if(kind==3)Off(e);
            else Back(e);
        }
        PathRevBuild();PathFold(table,since);Fresh(fresh,goal);
        if(Compare(trial,"random"))return 1;
    }
    puts("PASS folded tables equal fresh tables");
    return 0;
}
'''


def function(source, name):
    match = re.search(r'^static [^\n]+\b' + re.escape(name) + r'\(', source, re.M)
    if not match:
        raise ValueError(f'Missing production function {name}')
    brace = source.index('{', match.end())
    depth, end = 1, brace + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[match.start():end]


def block(source, first, last):
    start = source.index(first)
    return source[start:source.index(last, start) + len(last)]


class PathFold(unittest.TestCase):
    def test_folded_tables_equal_fresh_tables(self):
        cc = shutil.which('gcc') or shutil.which('clang')
        if not cc:
            self.skipTest('no C compiler')
        source = SOURCE.read_text(encoding='utf-8')
        extracted = '\n'.join([
            block(source, '#define PATH_DECREASE_RING', 'pathDecrease[i].increase=increase;\n}'),
            block(source, '#define PATH_UNREACHED', '} pathMark;'),
            function(source, 'PathHeapPush'),
            function(source, 'PathHeapPop'),
            function(source, 'PathRevBuild'),
            function(source, 'PathSettle'),
            function(source, 'PathOutBest'),
            block(source, 'static struct { unsigned int stamp[GRAPH_MAX_EDGES],now;int cost[GRAPH_MAX_EDGES]; } pathOld;',
                  '} pathOld;'),
            function(source, 'PathOldCost'),
            function(source, 'PathFold'),
        ])
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'fold.c').write_text(PRELUDE + extracted + MAIN, encoding='utf-8')
            exe = tmp / 'fold.exe'
            built = subprocess.run([cc, '-std=c99', '-O1', str(tmp / 'fold.c'), '-o', str(exe), '-lm'],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('PASS', run.stdout)


if __name__ == '__main__':
    unittest.main()
