"""Compile the release's learned-set reset and check it restores the loaded graph.

`Graph_ResetLearned` is what keeps a fixture's pre-release free play out of the
leg: the bot's own AI runs from its spawn until `bot_corner_start` arrives, and
every strafe edge it learns there joins the state planner's routes. The two
functions that do the work - `TouchEdge`, which copies a loaded edge before a
session mutates it, and `ResetLearned`, which puts the array and the per-cell
chains back - are extracted from `codemp/game/bot2_graph.c` and compiled, so
this is the shipped text and not a transcription of it.

It tests the reset's bookkeeping only. That the reset makes an unseeded start
class deterministic is a native measurement, recorded under
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
CORNER = ROOT / 'codemp/game/bot2_corner.c'
HEADER = ROOT / 'codemp/game/bot2_graph.h'

PRELUDE = r'''
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
#define GRAPH_MAX_CELLS 16
#define GRAPH_MAX_EDGES 16
typedef struct {int firstEdge;} graphCell_t;
typedef struct {int type,from,to,costMs,validated,traversals,failures,disabled,nextFrom;} graphEdge_t;
static struct {
    int count,edgeCount,capped,loadedEdges,loadedCapped;
    graphCell_t cell[GRAPH_MAX_CELLS];
    graphEdge_t edge[GRAPH_MAX_EDGES];
} graph;
/* The planners' graph generation: a reset must bump it, or a memoised route
   planned before the reset could be served after it. */
static unsigned int graphGeneration;
/* Graph_Reaches' cache key: a reset changes which edges exist. */
static unsigned int reachTopology;
/* The state planner's per-cell strafe lists are built from the chains the
   reset rebuilds, so a reset must drop them too. */
static unsigned int strafeNearGeneration;
static void StrafeNearChanged(void) {strafeNearGeneration++;}
/* The planner guidance's and route tables' key: a reset can move or restore any edge. */
static unsigned int guideTopo;
/* The dirty-list autosave: a reset marks the whole learned set for the next save. */
static int saveDirtyAll;
static void SaveDirtyAll(void) {saveDirtyAll=1;}
'''

MAIN = r'''
static int Append(int from,int to,int cost){
    graphEdge_t *e=&graph.edge[graph.edgeCount];memset(e,0,sizeof(*e));
    e->from=from;e->to=to;e->costMs=cost;
    e->nextFrom=graph.cell[from].firstEdge;graph.cell[from].firstEdge=graph.edgeCount;
    return graph.edgeCount++;
}

int main(void){
    int i,dropped,chain;
    /* A loaded graph: four cells, three edges, two of them leaving cell 1. */
    graph.count=4;
    for(i=0;i<graph.count;i++)graph.cell[i].firstEdge=-1;
    Append(1,2,800);Append(1,3,900);Append(0,1,700);
    graph.loadedEdges=graph.edgeCount;graph.loadedCapped=graph.capped=0;
    CHECK(graph.cell[1].firstEdge==1);
    CHECK(graph.edge[1].nextFrom==0);
    CHECK(graph.edge[0].nextFrom==-1);

    /* A session runs: it merges into a loaded edge, records a result on
       another, disables a third, and appends two edges of its own. */
    TouchEdge(0);graph.edge[0].costMs=625;graph.edge[0].traversals=3;
    TouchEdge(1);graph.edge[1].validated=1;graph.edge[1].failures=2;graph.edge[1].disabled=1;
    TouchEdge(1);                       /* a second mutation keeps the first copy */
    graph.edge[1].costMs=111;
    Append(1,0,500);Append(2,3,550);
    TouchEdge(4);                       /* an appended edge is not copied */
    CHECK(graph.edgeCount==5);
    CHECK(graph.cell[1].firstEdge==3);

    {unsigned int before=graphGeneration,nearBefore=strafeNearGeneration;
    dropped=ResetLearned();
    CHECK(graphGeneration!=before);CHECK(strafeNearGeneration!=nearBefore);}

    CHECK(dropped==2);
    CHECK(graph.edgeCount==3);
    CHECK(graph.capped==0);
    /* The loaded edges are exactly as they were loaded. */
    CHECK(graph.edge[0].costMs==800 && graph.edge[0].traversals==0);
    CHECK(graph.edge[1].costMs==900 && graph.edge[1].validated==0);
    CHECK(graph.edge[1].failures==0 && graph.edge[1].disabled==0);
    CHECK(graph.edge[2].costMs==700);
    /* The chains are the loaded graph's own, in the loaded order. */
    CHECK(graph.cell[0].firstEdge==2 && graph.edge[2].nextFrom==-1);
    CHECK(graph.cell[1].firstEdge==1 && graph.edge[1].nextFrom==0 && graph.edge[0].nextFrom==-1);
    CHECK(graph.cell[2].firstEdge==-1);
    CHECK(graph.cell[3].firstEdge==-1);
    /* Walking cell 1's chain reaches both of its loaded edges and no more. */
    for(i=0,chain=graph.cell[1].firstEdge;chain>=0;chain=graph.edge[chain].nextFrom)i++;
    CHECK(i==2);

    /* A second reset with nothing learned is a no-op. */
    CHECK(ResetLearned()==0);
    CHECK(graph.edgeCount==3 && graph.cell[1].firstEdge==1);
    puts("PASS learned-set reset restores the loaded graph");
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


class LearnedSetReset(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text(encoding='utf-8')

    def test_the_release_is_the_only_caller(self):
        """Production has no release, so nothing but the fixture's setup resets."""
        self.assertIn('void Graph_ResetLearned(void);', HEADER.read_text(encoding='utf-8'))
        self.assertIn('Graph_ResetLearned();', CORNER.read_text(encoding='utf-8'))
        callers = [path for path in sorted((ROOT / 'codemp/game').glob('*.c'))
                   if 'Graph_ResetLearned(' in path.read_text(encoding='utf-8', errors='replace')]
        self.assertEqual([p.name for p in callers], ['bot2_corner.c', 'bot2_graph.c'])

    def test_every_mutation_of_a_loaded_edge_is_copied_first(self):
        """A loaded edge is mutated in three places; each copies before it writes."""
        for name in ('Graph_AddStrafeEdge', 'Graph_EdgeCost', 'Graph_EdgeResult'):
            body = re.search(r'^\w[^\n]*\b' + name + r'\(.*?\n\}', self.source, re.M | re.S).group(0)
            self.assertIn('TouchEdge(', body, f'{name} mutates a loaded edge without copying it')

    def test_reset_restores_the_loaded_graph(self):
        cc = shutil.which('gcc') or shutil.which('clang')
        if not cc:
            self.skipTest('no C compiler')
        extracted = '\n'.join([
            re.search(r'^#define GRAPH_MAX_TOUCHED\s+\d+', self.source, re.M).group(0),
            re.search(r'^static struct \{\n    int count,overflow;.*?\n\} touched;', self.source, re.M | re.S).group(0),
            function(self.source, 'TouchEdge'),
            function(self.source, 'ResetLearned'),
        ])
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'reset.c').write_text(PRELUDE + extracted + MAIN, encoding='utf-8')
            exe = tmp / 'reset.exe'
            built = subprocess.run([cc, '-std=c99', '-O0', str(tmp / 'reset.c'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('PASS', run.stdout)


if __name__ == '__main__':
    unittest.main()
