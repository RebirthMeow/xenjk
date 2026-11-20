// Movement rewrite preparation (docs/movement/ARCHITECTURE.md, section 1).
// Node discovery. Cells are grid columns at a floor height; the fill admits
// a neighbour with Bot2_GroundSample, the one corridor sample the walk uses,
// from the current cell's standing point, so a linked pair of cells is a
// pair the walk can cross. Stacked floors in one column are distinct cells.
// Straight runs of linked cells along each axis are the nodes. Edges are
// maneuvers between cells: hops learned from committed gap plans, and pad,
// drop and jump candidates found from the world itself (bot_moveNativeEdges,
// the default) or, with it 0, seeded from the navmesh's off-mesh connections.
// The search is A* by time over links and edges. The result is cached per
// map checksum and physics binding under bot2-graph/, as text a tool can
// read, and every build is reported with its cost.
#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2.h"
#include "ai_bot2_internal.h"
#include "g_navmesh.h"
#include "bot2_move.h"
#include "bot2_graph.h"
#include "bot2_forecast.h"
#include "bot2_movecost.h"
#include "bot2_mover.h"
#include <stdlib.h>

// Review batch-2 fixes: on by default; a cvar set to 0
// turns its fix off (records/2026-11-10-review-batch2.md).
static int FixOn(const char *name) {char v[16];trap->Cvar_VariableStringBuffer(name,v,sizeof(v));return !v[0] || atoi(v)!=0;}

#define GRAPH_VERSION 4
#define GRAPH_MAX_CELLS 32768
#define GRAPH_MAX_NODES 16384
#define GRAPH_MAX_EDGES 131072
#define GRAPH_CACHE_LINE 320        // the writer's maximum bytes per record
#define GRAPH_CACHE_MAX_BYTES ((GRAPH_MAX_CELLS+GRAPH_MAX_NODES+GRAPH_MAX_EDGES+GRAPH_REASONS+1)*GRAPH_CACHE_LINE)
#define GRAPH_HASH 65536            // buckets; a power of two
#define GRAPH_MAX_SEEDS 64
#define GRAPH_SEED_RISE (2*STEPSIZE) // a seed's floor is sought from this far above its origin
#define GRAPH_SEED_DROP 128.0f       // and this far below
#define GRAPH_LAYER_TOLERANCE .5f    // two admitted heights this close in one column are one floor
#define GRAPH_HINT_RADIUS 48.0f      // an off-mesh end binds to a cell this close
#define GRAPH_HINT_HEIGHT 48.0f
#define GRAPH_WALK_MS 128            // one 32-unit link at the 250 u/s run
#define GRAPH_DIAGONAL_MS 181        // one diagonal step across a square of four linked cells
#define GRAPH_HEURISTIC_SPEED 450.0f // fastest sustained speed the search assumes
#define GRAPH_EDGE_FAILURES 2        // failures before an edge leaves the search
#define GRAPH_MOVER_OFF_MS 5000      // a mover's failure: about one lift wait and travel
#define GRAPH_PROVEN_OFF_MS 30000    // a validated edge's failures against the world
#define GRAPH_REFINE_SAMPLE 24.0f    // the walk corridor's own sample spacing
// The state search. Speed is banded for the state key; a strafe edge admits
// a landing state whose speed is within the band of its entry speed and
// whose heading is within the cone of its entry heading, from a cell within
// the reach of its takeoff (the walk's own path reach). The walk's law over
// a link: ground friction and the run's acceleration per command frame, as
// PM_Friction and PM_Accelerate apply them (PHYSICS.md).
#define GRAPH_STATE_BAND 48.0f
#define GRAPH_STATE_CONE 30.0f
#define GRAPH_STATE_REACH 48.0f
#define GRAPH_STATE_MAX 131072       // state entries the search may touch; a power of two
#define GRAPH_PLAN_SPLITS 4          // state searches that may run split over frames at once (about 11 MB a table)
#define GRAPH_STATE_AVOID 64         // avoided edges a search keeps (a split search keeps its own copy)
#define GRAPH_STATE_STEP_MS 25       // the command frame the walk's law is applied over
#define GRAPH_FRICTION 6.0f
#define GRAPH_STOP_SPEED 100.0f
#define GRAPH_ACCEL 10.0f
#define GRAPH_SPEED_FACTOR 1.7f      // Force Speed's wishspeed multiplier
#define GRAPH_CORRIDOR_RADIUS 192.0f // the guided corridor's default half-width

static const vec3_t graphMins={-15,-15,DEFAULT_MINS_2};
static const vec3_t graphMaxs={15,15,DEFAULT_MAXS_2};
static const int graphDir[4][2]={{1,0},{0,1},{-1,0},{0,-1}};
static const char *const graphSeedClasses[]={
    "team_CTF_redflag","team_CTF_blueflag","team_CTF_redplayer","team_CTF_blueplayer",
    "team_CTF_redspawn","team_CTF_bluespawn","info_player_deathmatch"
};
// Every refusal Bot2_GroundSample can name, in the order the report lists them.
static const char *const graphReasons[]={
    "floor_startsolid","floor_allsolid","floor_missing","floor_liquid","floor_nonworld","floor_steep",
    "ascent","drop","sweep_liquid","descent_cross_blocked","descent_settle_blocked",
    "step_up_blocked","step_cross_blocked","hurt_saturation","trigger_hurt","other"
};
#define GRAPH_REASONS ((int)ARRAY_LEN(graphReasons))

static struct {
    int count,nodeCount,edgeCount,seeds,seedRefused,samples,refusals,links,refined,boundary,capped,elapsedMs,cached;
    int hints,hintsBound;
    int nativePads,nativeDrops,nativeJumps; // edges added by Pads() and DropsAndJumps()
    int climbs;             // climb edges added by Climbs()
    int lifts;              // lift edges added by Lifts()
    int loadedEdges,loadedCapped;   // the learned set as loaded; Graph_ResetLearned restores it
    int reason[GRAPH_REASONS];
    char map[64],checksum[64];
    char identity[24];              // Graph_Identity: the contents, frozen at load
    int gravity,speed;
    int head[GRAPH_HASH];
    graphCell_t cell[GRAPH_MAX_CELLS];
    int chain[GRAPH_MAX_CELLS];     // next cell in the same hash bucket
    graphNode_t node[GRAPH_MAX_NODES];
    graphEdge_t edge[GRAPH_MAX_EDGES];
} graph;

// Search scratch, in BSS: a binary heap keyed by f, per-cell g and parents.
// The heap holds a cell once per improvement of its g, so it is sized by the
// pushes a search can make, not by the cells: one per successful relaxation,
// and each cell is expanded once over its four links, four diagonals and edges.
#define GRAPH_SEARCH_HEAP (GRAPH_MAX_CELLS*8+GRAPH_MAX_EDGES+1)
static struct {
    float g[GRAPH_MAX_CELLS];
    int parent[GRAPH_MAX_CELLS],parentEdge[GRAPH_MAX_CELLS];
    unsigned char state[GRAPH_MAX_CELLS]; // 0 untouched, 1 open, 2 closed
    int heap[GRAPH_SEARCH_HEAP];float key[GRAPH_SEARCH_HEAP];int heapCount;
    int stamp,cellStamp[GRAPH_MAX_CELLS];
} search;

// ---------------------------------------------------------------------------
// The guided corridor. A coarse polyline of positions from a human
// demonstration, with a radius: the searches may only expand cells within
// that distance of it. Nothing about how the bot moves comes from here, and
// with no corridor loaded every admission below returns 1, so the unguided
// searches are the same code running unrestricted. Admission is precomputed
// per cell when the corridor is loaded, because the searches ask per relax.
// ---------------------------------------------------------------------------
static struct {
    int count,cells;
    float radius;
    vec3_t point[GRAPH_MAX_CORRIDOR];
    unsigned char admits[GRAPH_MAX_CELLS];
    char name[MAX_QPATH];
} corridor;

// Distance from a point to one segment, in three dimensions: a route's
// height separates the floors a column carries.
static float SegmentDistance(const vec3_t a,const vec3_t b,const vec3_t p) {
    vec3_t ab,ap,at;float len,t;
    VectorSubtract(b,a,ab);VectorSubtract(p,a,ap);
    len=DotProduct(ab,ab);
    if(len<.0001f)return VectorLength(ap);
    t=DotProduct(ap,ab)/len;
    if(t<0)t=0;else if(t>1)t=1;
    VectorMA(a,t,ab,at);
    return Distance(at,p);
}

float Graph_CorridorDistance(const vec3_t point) {
    int i;float best;
    if(corridor.count<1)return -1;
    if(corridor.count==1)return Distance(corridor.point[0],point);
    best=SegmentDistance(corridor.point[0],corridor.point[1],point);
    for(i=1;i<corridor.count-1;i++) {
        float d=SegmentDistance(corridor.point[i],corridor.point[i+1],point);
        if(d<best)best=d;
    }
    return best;
}

int Graph_CorridorPoints(void) {return corridor.count;}
int Graph_CorridorCells(void) {return corridor.cells;}
float Graph_CorridorRadius(void) {return corridor.radius;}
int Graph_CorridorAdmits(int cell) {
    if(corridor.count<1)return 1;
    if(cell<0 || cell>=graph.count)return 0;
    return corridor.admits[cell];
}

static unsigned int graphGeneration;
// The earliest timed off still pending (0 none). Its end is a change the
// planners can read, so it bumps the generation, lazily: here, when a planner
// next asks, and before a search reads the edges.
static int offNext;
// What Graph_Reaches depends on: the cells' links, each edge's from/to and whether it is off (disabled, or
// offUntil in the future). Bumped wherever one of those can change (an edge added, failed, expired or toggled, a
// load); a cost, a traversal, a result or a merge changes none of them, so they bump only graphGeneration.
static unsigned int reachTopology;
// What the planner guidance's arcs are built from (bot_movePlanGuide): bumped on a load or a reset of the learned
// edges, which can move or restore anything; an edge added, merged or made cheaper is taken incrementally.
static unsigned int guideTopo;
// The cell-search route tables (Graph_PathTable): an edge change is queued here and each table folds it in when
// next asked. An edge made available or cheaper is relaxed incrementally; one turned off, disabled or made dearer
// rebuilds only a table whose cheapest routes used it (its arc was tight there). The cells' clearance bumps pathFull,
// a load or a reset guideTopo; either rebuilds every table. `cost` is the edge's cost before the change, or -1 when
// it was no arc before it (added, or back from off).
#define PATH_DECREASE_RING 256
static unsigned int pathFull,pathDecreaseSeq;
static struct { int edge,cost,increase; } pathDecrease[PATH_DECREASE_RING];
static void PathChange(int edge,int cost,int increase) {
    int i=pathDecreaseSeq++%PATH_DECREASE_RING;
    pathDecrease[i].edge=edge;pathDecrease[i].cost=cost;pathDecrease[i].increase=increase;
}
// bot_moveFixBP9 (default 1): a pre-search runs the search core with level.time set to the
// predicted boundary, so the graph's own clocks (edge time-outs, fold and spread timers) read the real frame's time,
// set once a frame by Graph_Frame from G_RunFrame. 0 reads level.time, as before. A frame time later than
// level.time (a new map before its first frame) is not used.
static int graphFrameTime,graphFrameFix;
void Graph_Frame(void) {graphFrameTime=level.time;graphFrameFix=FixOn("bot_moveFixBP9")>0;}
static int Now(void) {return graphFrameFix && graphFrameTime<=level.time?graphFrameTime:level.time;}

static void OffExpire(void) {
    int i,now=Now();
    if(!offNext || now<offNext)return;
    offNext=0;graphGeneration++;reachTopology++;
    for(i=0;i<graph.edgeCount;i++) {
        graphEdge_t *e=&graph.edge[i];
        if(!e->offUntil)continue;
        if(e->offUntil<=now) {
            e->offUntil=0;PathChange(i,-1,0);
            trap->Print("[GRAPH] edge_back index=%d level=%d\n",i,now);
        } else if(!offNext || e->offUntil<offNext)offNext=e->offUntil;
    }
}
unsigned int Graph_Generation(void) {OffExpire();return graphGeneration;}
int Graph_EdgeOff(const graphEdge_t *e) {return e->disabled || e->offUntil>Now();}

// What the state search's per-cell strafe lists are built from: the cells'
// links, the edge chains and each strafe edge's takeoff. Bumped wherever one
// of those can change; a cost, a result or a merge that keeps the takeoff
// leaves the lists valid, since the search reads those fields live.
static unsigned int strafeNearGeneration;
static void StrafeNearChanged(void) {strafeNearGeneration++;}

void Graph_CorridorClear(void) {memset(&corridor,0,sizeof(corridor));graphGeneration++;}

// Which cells the polyline admits, once per load.
static void CorridorMark(void) {
    int i;vec3_t point;
    graphGeneration++;
    corridor.cells=0;
    memset(corridor.admits,0,sizeof(corridor.admits));
    for(i=0;i<graph.count;i++) {
        point[0]=graph.cell[i].ix*(float)GRAPH_GRID;point[1]=graph.cell[i].iy*(float)GRAPH_GRID;point[2]=graph.cell[i].z;
        if(Graph_CorridorDistance(point)<=corridor.radius) {corridor.admits[i]=1;corridor.cells++;}
    }
}

int Graph_CorridorLoad(const char *name,float radius,char *why,int whySize) {
    char path[MAX_QPATH],map[64],checksum[64],*bytes,*line,*next;
    fileHandle_t file=0;int size,version,points,count=0,ok=1;
    Graph_CorridorClear();
    if(why && whySize>0)*why=0;
    if(!name || !name[0])return 0;
    if(strchr(name,'.') && strstr(name,"..")) {if(why)Q_strncpyz(why,"bad_name",whySize);return 0;}
    Com_sprintf(path,sizeof(path),"bot2-corridor/%s",name);
    size=trap->FS_Open(path,&file,FS_READ);
    if(!file) {if(why)Q_strncpyz(why,"missing",whySize);return 0;}
    if(size<=0 || size>GRAPH_MAX_CORRIDOR*GRAPH_CACHE_LINE) {
        trap->FS_Close(file);if(why)Q_strncpyz(why,"size",whySize);return 0;
    }
    bytes=(char*)malloc(size+1);
    if(!bytes) {trap->FS_Close(file);if(why)Q_strncpyz(why,"memory",whySize);return 0;}
    trap->FS_Read(bytes,size,file);trap->FS_Close(file);bytes[size]=0;
    next=strchr(bytes,'\n');if(next)*next++=0;
    if(sscanf(bytes,"corridor %d map=%63s checksum=%63s points=%d",&version,map,checksum,&points)!=4 ||
       version!=1 || strcmp(map,graph.map) || strcmp(checksum,graph.checksum) || points<2 || points>GRAPH_MAX_CORRIDOR) {
        free(bytes);if(why)Q_strncpyz(why,"binding",whySize);return 0;
    }
    for(line=next;line && ok;line=next) {
        next=strchr(line,'\n');if(next)*next++=0;
        if(line[0]=='p') {
            vec3_t p;
            if(count>=points || sscanf(line,"p %f %f %f",&p[0],&p[1],&p[2])!=3) {ok=0;break;}
            VectorCopy(p,corridor.point[count]);count++;
        } else if(line[0] && line[0]!='\r' && line[0]!='#')ok=0;
    }
    free(bytes);
    if(!ok || count!=points) {Graph_CorridorClear();if(why)Q_strncpyz(why,"malformed",whySize);return 0;}
    corridor.count=count;
    corridor.radius=radius>0?radius:GRAPH_CORRIDOR_RADIUS;
    Q_strncpyz(corridor.name,name,sizeof(corridor.name));
    CorridorMark();
    if(!corridor.cells) {Graph_CorridorClear();if(why)Q_strncpyz(why,"no_cells",whySize);return 0;}
    return corridor.count;
}

int Graph_CellCount(void) {return graph.count;}
int Graph_NodeCount(void) {return graph.nodeCount;}
int Graph_EdgeCount(void) {return graph.edgeCount;}
const graphCell_t *Graph_Cell(int index) {return index>=0 && index<graph.count?&graph.cell[index]:NULL;}
const graphNode_t *Graph_Node(int index) {return index>=0 && index<graph.nodeCount?&graph.node[index]:NULL;}
const graphEdge_t *Graph_Edge(int index) {return index>=0 && index<graph.edgeCount?&graph.edge[index]:NULL;}

static unsigned int Bucket(int ix,int iy) {
    return ((unsigned int)ix*73856093u ^ (unsigned int)iy*19349663u)&(GRAPH_HASH-1);
}

// The cell in a column whose height is within the tolerance, or -1.
static int Lookup(int ix,int iy,float z,float tolerance) {
    int i;
    for(i=graph.head[Bucket(ix,iy)];i>=0;i=graph.chain[i])
        if(graph.cell[i].ix==ix && graph.cell[i].iy==iy && fabsf(graph.cell[i].z-z)<=tolerance)return i;
    return -1;
}

int Graph_CellAt(const vec3_t point,float tolerance) {
    return Lookup((int)floorf(point[0]/GRAPH_GRID+.5f),(int)floorf(point[1]/GRAPH_GRID+.5f),point[2],tolerance);
}

int Graph_Nearest(const vec3_t point,float radius,float tolerance) {
    int cx=(int)floorf(point[0]/GRAPH_GRID+.5f),cy=(int)floorf(point[1]/GRAPH_GRID+.5f);
    int span=(int)ceilf(radius/GRAPH_GRID),ix,iy,i,best=-1;float bestD=radius*radius+1;
    for(ix=cx-span;ix<=cx+span;ix++)for(iy=cy-span;iy<=cy+span;iy++)
        for(i=graph.head[Bucket(ix,iy)];i>=0;i=graph.chain[i]) {
            const graphCell_t *c=&graph.cell[i];float dx,dy,d;
            if(c->ix!=ix || c->iy!=iy || fabsf(c->z-point[2])>tolerance)continue;
            dx=ix*(float)GRAPH_GRID-point[0];dy=iy*(float)GRAPH_GRID-point[1];d=dx*dx+dy*dy;
            if(d<=radius*radius && d<bestD) {bestD=d;best=i;}
        }
    return best;
}

int Graph_Linked(int a,int b) {
    int d;
    if(a<0 || b<0 || a>=graph.count || b>=graph.count)return 0;
    for(d=0;d<4;d++)if(graph.cell[a].link[d]==b)return 1;
    return 0;
}

// Breadth first with each cell visited once. A walk-tree frontier capped at 64
// entries was exact to four links and partial beyond.
int Graph_LinkedWithin(int a,int b,int steps) {
    static unsigned int stamp[GRAPH_MAX_CELLS],now;static int queue[GRAPH_MAX_CELLS];
    int head=0,tail=0,end,depth,d;
    if(a<0 || b<0)return 0;
    if(a==b)return 1;
    if(a>=graph.count || b>=graph.count)return 0;
    if(++now==0) {memset(stamp,0,sizeof(stamp));now=1;}
    stamp[a]=now;queue[tail++]=a;
    for(depth=0;depth<steps && head<tail;depth++)
        for(end=tail;head<end;head++)for(d=0;d<4;d++) {
            int n=graph.cell[queue[head]].link[d];
            if(n<0 || stamp[n]==now)continue;
            if(n==b)return 1;
            stamp[n]=now;queue[tail++]=n;
        }
    return 0;
}

static int AddCell(int ix,int iy,float z,int seed) {
    graphCell_t *c;unsigned int b;
    if(graph.count>=GRAPH_MAX_CELLS) {graph.capped=1;return -1;}
    c=&graph.cell[graph.count];memset(c,0,sizeof(*c));
    c->ix=ix;c->iy=iy;c->z=z;c->seed=seed;c->link[0]=c->link[1]=c->link[2]=c->link[3]=-1;c->node[0]=c->node[1]=-1;c->firstEdge=-1;
    b=Bucket(ix,iy);graph.chain[graph.count]=graph.head[b];graph.head[b]=graph.count;
    return graph.count++;
}

static int autosave=1;
void Graph_Autosave(int on) {Graph_SaveFlush();autosave=on;}

// ---------------------------------------------------------------------------
// The deferred cache write (`bot_moveGraphSaveDefer`, default on; 0 restores the immediate write).
//
// An autosave is a full rewrite of the cache - 0.9 MB on cache v1, 1.27 MB on
// v2 - on the frame that learned, 43 to 57 times a cost world, and every
// frame of half a second or more in the four production-cache cost worlds
// coincided with one (docs/movement/records/2026-09-22-boundary-cost-
// attribution.md). With the cvar set, an autosave instead copies what can have
// changed since the load - the edge records and the capped flag; cells, nodes
// and the header's other counts are fixed once the graph is loaded - and the
// last copy is written when the session ends, or when anything is about to
// change what a write would contain. Because the snapshot is taken where the
// write happens today, the file finally written is byte for byte the one the
// last autosave would have written, staleness included (a cost update or a
// failure after the last autosave is not written, as today). Nothing reads
// the file inside a session - `Load`, at map load, is its only reader - so no
// decision can change.
// ---------------------------------------------------------------------------
static struct {
    int pending,edgeCount,capped,requests;
    graphEdge_t edge[GRAPH_MAX_EDGES];
} saveSnapshot;

// the edges whose saved fields changed since the last snapshot, so an autosave copies those and
// the edges appended since, not the whole array. Every writer of a field WriteCache writes marks its index: the
// strafe merge, Graph_EdgeCost, a first validation; Reset and ResetLearned (and a full list) mark all. Fields the file
// does not carry (traversals, failures, disabled, offUntil, nextFrom) go stale in the snapshot; nothing reads them
// there. The file is byte for byte the one a whole copy writes (tools/test_movement_graph_save_defer.py).
#define SAVE_DIRTY_MAX 4096
static struct {
    int all,count,index[SAVE_DIRTY_MAX];
    unsigned char mark[GRAPH_MAX_EDGES];
} saveDirty={1};

static void SaveDirty(int index) {
    if(saveDirty.all || index<0 || index>=GRAPH_MAX_EDGES || saveDirty.mark[index])return;
    if(saveDirty.count>=SAVE_DIRTY_MAX) {saveDirty.all=1;return;}
    saveDirty.mark[index]=1;saveDirty.index[saveDirty.count++]=index;
}
static void SaveDirtyAll(void) {saveDirty.all=1;}

// Default on: unset or any positive value defers;
// 0 restores the immediate write on every autosave. A deferred session
// flushes at shutdown, and at each leg boundary (Move_Plan's objective change)
// only with bot_moveGraphSaveLeg set: in live play those flushes stalled the
// server.
static int SaveDeferred(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveGraphSaveDefer",value,sizeof(value));
    return !value[0] || atoi(value)>0;
}

static qboolean SaveEdges(const graphEdge_t *edges,int edgeCount,int capped);

// What each autosave site calls: today's write, or with the deferral on a
// snapshot of what that write would have contained.
static qboolean Autosave(void) {
    int i;
    if(!SaveDeferred())return Graph_Save();
    if(saveDirty.all || graph.edgeCount<saveSnapshot.edgeCount) {
        if(graph.edgeCount>0)memcpy(saveSnapshot.edge,graph.edge,graph.edgeCount*sizeof(graphEdge_t));
    } else {
        for(i=0;i<saveDirty.count;i++) {
            int k=saveDirty.index[i];
            if(k<saveSnapshot.edgeCount)saveSnapshot.edge[k]=graph.edge[k];
        }
        if(graph.edgeCount>saveSnapshot.edgeCount)memcpy(saveSnapshot.edge+saveSnapshot.edgeCount,graph.edge+saveSnapshot.edgeCount,
            (graph.edgeCount-saveSnapshot.edgeCount)*sizeof(graphEdge_t));
    }
    for(i=0;i<saveDirty.count;i++)saveDirty.mark[saveDirty.index[i]]=0;
    saveDirty.count=0;saveDirty.all=0;
    saveSnapshot.edgeCount=graph.edgeCount;saveSnapshot.capped=graph.capped;
    saveSnapshot.pending=1;saveSnapshot.requests++;
    return qtrue;
}

qboolean Graph_SaveFlush(void) {
    qboolean ok;
    if(!saveSnapshot.pending)return qtrue;
    saveSnapshot.pending=0;
    ok=SaveEdges(saveSnapshot.edge,saveSnapshot.edgeCount,saveSnapshot.capped);
    trap->Print("[GRAPH] cache_flush edges=%d capped=%d requests=%d ok=%d\n",
        saveSnapshot.edgeCount,saveSnapshot.capped,saveSnapshot.requests,ok?1:0);
    saveSnapshot.requests=0;
    if(!ok)trap->Print("[GRAPH] cache_write_failed\n");
    return ok;
}

// ---------------------------------------------------------------------------
// The learned set, and putting it back.
//
// A fixture world does not begin at the release. The bench brings a world up,
// the bot's own AI plays from its spawn, and only when the world reaches the
// pinned level does `bot_corner_start` place it at the case's pose. Everything
// the search commits during that free play is learned into the graph, and a
// learned strafe edge changes what the state planner can route over: measured
// on ten retained unseeded worlds pinned at one level, one pre-release edge
// (cell 47 to 4949) put a strafe edge on the first post-release route and cost
// 2,975 ms, while another (61 to 6167) was never on a route and cost nothing.
// Worlds that learned the same set returned the same trip decision for
// decision; worlds that learned different sets did not.
//
// So the release puts the learned set back to the graph as loaded. Appended
// edges are dropped - appending never moves an existing index, so truncating
// is exact - and the loaded edges a session mutated are restored from a
// copy taken before the first mutation. Nothing outside `bot_corner_start`
// calls this, so a real spawn is unchanged: production never restores,
// because production has no release to restore at.
// ---------------------------------------------------------------------------
#define GRAPH_MAX_TOUCHED 256
static struct {
    int count,overflow;
    int index[GRAPH_MAX_TOUCHED];
    graphEdge_t record[GRAPH_MAX_TOUCHED];
} touched;

// Copy a loaded edge before a session mutates it. Appended edges need no
// copy, since the reset drops them outright.
static void TouchEdge(int index) {
    int i;
    if(index<0 || index>=graph.loadedEdges)return;
    for(i=0;i<touched.count;i++)if(touched.index[i]==index)return;
    if(touched.count>=GRAPH_MAX_TOUCHED) {touched.overflow++;return;}
    touched.index[touched.count]=index;touched.record[touched.count]=graph.edge[index];touched.count++;
}

// The learned set as the graph was loaded, frozen where the identity is.
static void FreezeLoaded(void) {
    graph.loadedEdges=graph.edgeCount;graph.loadedCapped=graph.capped;
    touched.count=0;touched.overflow=0;
}

// The reset itself, without the print or the cache write, so a regression can
// compile it. Restores every touched loaded edge, drops the appended ones and
// rebuilds the per-cell chains exactly as the adds built them - forward, each
// edge prepended - so the chain order is the loaded graph's own. Returns the
// number of edges dropped.
static int ResetLearned(void) {
    int i,dropped=graph.edgeCount-graph.loadedEdges;
    graphGeneration++;reachTopology++;StrafeNearChanged();guideTopo++;SaveDirtyAll();
    for(i=0;i<touched.count;i++)graph.edge[touched.index[i]]=touched.record[i];
    touched.count=0;
    graph.edgeCount=graph.loadedEdges;graph.capped=graph.loadedCapped;
    for(i=0;i<graph.count;i++)graph.cell[i].firstEdge=-1;
    for(i=0;i<graph.edgeCount;i++) {
        graphEdge_t *e=&graph.edge[i];
        e->nextFrom=graph.cell[e->from].firstEdge;graph.cell[e->from].firstEdge=i;
    }
    return dropped;
}

void Graph_ResetLearned(void) {
    int restored=touched.count,overflow=touched.overflow,dropped;
    dropped=ResetLearned();
    trap->Print("[GRAPH] reset_learned dropped=%d restored=%d overflow=%d edges=%d graph=%s\n",
        dropped,restored,overflow,graph.edgeCount,Graph_Identity());
    touched.overflow=0;
    if((dropped || restored) && autosave && !Autosave())trap->Print("[GRAPH] cache_write_failed\n");
}

static void Reset(void) {
    graphGeneration++;reachTopology++;StrafeNearChanged();offNext=0;guideTopo++;SaveDirtyAll();
    memset(&graph,0,sizeof(graph));
    memset(graph.head,-1,sizeof(graph.head));
    // Cell indices are about to change, so the corridor's admission marks
    // are void. The caller loads it again against the cells it gets.
    Graph_CorridorClear();
}

static int ReasonIndex(const char *reason) {
    int i;
    for(i=0;i<GRAPH_REASONS-1;i++)if(!strcmp(reason,graphReasons[i]))return i;
    return GRAPH_REASONS-1;
}

static unsigned char ReasonHazard(const char *reason) {
    if(!strcmp(reason,"floor_missing") || !strcmp(reason,"drop"))return GRAPH_HAZARD_DROP;
    if(!strcmp(reason,"floor_liquid") || !strcmp(reason,"sweep_liquid"))return GRAPH_HAZARD_LIQUID;
    if(!strcmp(reason,"trigger_hurt") || !strcmp(reason,"hurt_saturation"))return GRAPH_HAZARD_HURT;
    if(!strcmp(reason,"ascent"))return GRAPH_HAZARD_ASCENT;
    if(!strcmp(reason,"floor_steep") || !strcmp(reason,"floor_nonworld"))return GRAPH_HAZARD_STEEP;
    return GRAPH_HAZARD_BLOCKED;
}

// Seed a column from an entity origin: the floor under the nearest grid
// point, sought with the player hull from a step above the origin. Returns
// the cell, or -1 with the refusal named.
static int Seed(const vec3_t origin,const char **reason) {
    int ix=(int)floorf(origin[0]/GRAPH_GRID+.5f),iy=(int)floorf(origin[1]/GRAPH_GRID+.5f),existing;
    vec3_t top,bottom;trace_t tr;float z;
    top[0]=ix*(float)GRAPH_GRID;top[1]=iy*(float)GRAPH_GRID;top[2]=origin[2]+GRAPH_SEED_RISE;
    VectorCopy(top,bottom);bottom[2]=origin[2]-GRAPH_SEED_DROP;
    trap->Trace(&tr,top,graphMins,graphMaxs,bottom,ENTITYNUM_NONE,MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME,qfalse,0,0);
    graph.samples++;
    *reason=tr.startsolid?"floor_startsolid":tr.allsolid?"floor_allsolid":tr.fraction==1?"floor_missing":
        (tr.contents&(CONTENTS_LAVA|CONTENTS_SLIME))?"floor_liquid":tr.entityNum!=ENTITYNUM_WORLD?"floor_nonworld":
        tr.plane.normal[2]<.7f?"floor_steep":NULL;
    if(*reason)return -1;
    z=tr.endpos[2]+.125f;
    existing=Lookup(ix,iy,z,STEPSIZE+.25f);
    if(existing>=0) {*reason="reached";return existing;}
    return AddCell(ix,iy,z,1);
}

static void Seeds(void) {
    int i;
    for(i=0;i<(int)ARRAY_LEN(graphSeedClasses);i++) {
        gentity_t *ent=NULL;
        while((ent=G_Find(ent,FOFS(classname),graphSeedClasses[i]))!=NULL) {
            const char *reason;int cell;
            if(graph.seeds>=GRAPH_MAX_SEEDS)return;
            graph.seeds++;
            cell=Seed(ent->s.origin,&reason);
            if(cell<0)graph.seedRefused++;
            trap->Print("[GRAPH] seed class=%s x=%.1f y=%.1f z=%.1f status=%s cell=%d\n",
                graphSeedClasses[i],ent->s.origin[0],ent->s.origin[1],ent->s.origin[2],
                reason?reason:"admitted",cell);
        }
    }
}

// The fill. Cells are visited in admission order; each samples toward its
// four neighbours once, and a pair already linked from the other side is
// not sampled again.
// A cell whose floor is a resting mover's (bot2_mover.h) names the mover by
// model and the position it rested at. It is floor only while that mover
// rests there (Graph_CellStands); the searches do not enter it otherwise.
static void CellFloor(int cell,int floor) {
    if(floor==ENTITYNUM_WORLD || floor<0 || floor>=ENTITYNUM_WORLD)return;
    graph.cell[cell].mover=(short)Bot2_MoverModel(floor);
    graph.cell[cell].moverState=(unsigned char)g_entities[floor].moverState;
}

int Graph_CellStands(int cell) {
    const graphCell_t *c;int e;
    if(cell<0 || cell>=graph.count)return 0;
    c=&graph.cell[cell];
    if(!c->mover)return 1;
    e=Bot2_MoverEntity(c->mover);
    return e>=0 && (int)g_entities[e].moverState==c->moverState && Bot2_StandableEntity(e);
}

static void Fill(void) {
    int i,d;
    for(i=0;i<graph.count && !graph.capped;i++) {
        for(d=0;d<4;d++) {
            graphCell_t *c=&graph.cell[i];
            int tx=c->ix+graphDir[d][0],ty=c->iy+graphDir[d][1],hit,other;
            vec3_t previous,point;const char *reason;
            if(c->link[d]>=0)continue;
            previous[0]=c->ix*(float)GRAPH_GRID;previous[1]=c->iy*(float)GRAPH_GRID;previous[2]=c->z;
            point[0]=tx*(float)GRAPH_GRID;point[1]=ty*(float)GRAPH_GRID;point[2]=c->z;
            graph.samples++;
            reason=Bot2_GroundSample(ENTITYNUM_NONE,graphMins,graphMaxs,previous,point,1,&hit);
            if(reason && (!strcmp(reason,"floor_startsolid") || !strcmp(reason,"floor_missing") ||
                          !strcmp(reason,"ascent") || !strcmp(reason,"drop"))) {
                // A 32-unit chord can refuse a ramp or stair the walk's
                // 24-unit corridor sample admits (the floor rises or falls
                // more than a step over the longer chord). Retry as the
                // walk would see it: one sample at its own spacing, then the
                // remainder. Both must admit; the link is then the walk's.
                vec3_t mid;const char *first;int midHit;
                mid[0]=previous[0]+graphDir[d][0]*GRAPH_REFINE_SAMPLE;mid[1]=previous[1]+graphDir[d][1]*GRAPH_REFINE_SAMPLE;mid[2]=previous[2];
                graph.samples++;
                first=Bot2_GroundSample(ENTITYNUM_NONE,graphMins,graphMaxs,previous,mid,1,&midHit);
                if(!first) {
                    point[0]=tx*(float)GRAPH_GRID;point[1]=ty*(float)GRAPH_GRID;point[2]=mid[2];
                    graph.samples++;
                    reason=Bot2_GroundSample(ENTITYNUM_NONE,graphMins,graphMaxs,mid,point,2,&hit);
                    if(!reason)graph.refined++;
                }
            }
            if(reason) {
                c->refused|=1<<d;c->flags|=ReasonHazard(reason);
                graph.refusals++;graph.reason[ReasonIndex(reason)]++;
                continue;
            }
            other=Lookup(tx,ty,point[2],GRAPH_LAYER_TOLERANCE);
            if(other<0 && (other=AddCell(tx,ty,point[2],0))>=0)CellFloor(other,Bot2_GroundSampleFloor());
            if(other<0)break;   // capped; the report says so
            c->link[d]=other;graph.cell[other].link[(d+2)&3]=i;graph.links++;
        }
    }
    for(i=0;i<graph.count;i++)if(graph.cell[i].refused)graph.boundary++;
}

// Runs along one axis. A run starts at a cell with no linked predecessor and
// follows the links forward; every cell lands in exactly one run per axis.
static void Runs(int axis) {
    int i,forward=axis,backward=axis+2;
    for(i=0;i<graph.count;i++) {
        graphNode_t *n;int j;
        if(graph.cell[i].link[backward]>=0 || graph.cell[i].node[axis]>=0)continue;
        if(graph.nodeCount>=GRAPH_MAX_NODES) {graph.capped=1;return;}
        n=&graph.node[graph.nodeCount];memset(n,0,sizeof(*n));
        n->axis=axis;n->first=i;n->zMin=n->zMax=graph.cell[i].z;
        for(j=i;j>=0 && graph.cell[j].node[axis]<0;j=graph.cell[j].link[forward]) {
            graph.cell[j].node[axis]=graph.nodeCount;n->count++;
            n->zMin=min(n->zMin,graph.cell[j].z);n->zMax=max(n->zMax,graph.cell[j].z);
            n->flags|=graph.cell[j].flags;
        }
        graph.nodeCount++;
    }
}

static void Widths(void) {
    int i;
    for(i=0;i<graph.nodeCount;i++)graph.node[i].width=1e9f;
    for(i=0;i<graph.count;i++) {
        const graphCell_t *c=&graph.cell[i];
        if(c->node[0]>=0 && c->node[1]>=0) {
            graph.node[c->node[0]].width=min(graph.node[c->node[0]].width,graph.node[c->node[1]].count*(float)GRAPH_GRID);
            graph.node[c->node[1]].width=min(graph.node[c->node[1]].width,graph.node[c->node[0]].count*(float)GRAPH_GRID);
        }
    }
}

static void Nodes(void) {
    Runs(1);Runs(0);Widths();
}

// ---------------------------------------------------------------------------
// Edges.
// ---------------------------------------------------------------------------
static const char *EdgeTypeName(int type) {
    switch(type) {
    case GRAPH_EDGE_HOP:return "hop";
    case GRAPH_EDGE_PAD:return "pad";
    case GRAPH_EDGE_DROP:return "drop";
    case GRAPH_EDGE_JUMP:return "jump";
    case GRAPH_EDGE_STRAFE:return "strafe";
    case GRAPH_EDGE_CLIMB:return "climb";
    case GRAPH_EDGE_LIFT:return "lift";
    }
    return "none";
}

static int AddEdgeRecord(int type,int from,int to,const vec3_t takeoff,const vec3_t landing,const float *dir,int costMs,int validated) {
    graphEdge_t *e;
    if(from<0 || to<0 || from>=graph.count || to>=graph.count || from==to)return -1;
    if(graph.edgeCount>=GRAPH_MAX_EDGES) {graph.capped=1;return -1;}
    graphGeneration++;reachTopology++;StrafeNearChanged();
    e=&graph.edge[graph.edgeCount];memset(e,0,sizeof(*e));
    e->type=type;e->from=from;e->to=to;VectorCopy(takeoff,e->takeoff);VectorCopy(landing,e->landing);
    e->dir[0]=dir[0];e->dir[1]=dir[1];e->costMs=costMs;e->validated=validated;
    e->nextFrom=graph.cell[from].firstEdge;graph.cell[from].firstEdge=graph.edgeCount;
    PathChange(graph.edgeCount,-1,0);
    return graph.edgeCount++;
}

int Graph_AddEdge(int type,int from,int to,const vec3_t takeoff,const vec3_t landing,const float *dir,int costMs,int validated) {
    int index=AddEdgeRecord(type,from,to,takeoff,landing,dir,costMs,validated);
    if(index>=0) {
        const graphEdge_t *e=&graph.edge[index];
        trap->Print("[GRAPH] edge_added index=%d type=%s from=%d to=%d takeoff=%.1f,%.1f,%.1f landing=%.1f,%.1f,%.1f cost_ms=%d validated=%d\n",
            index,EdgeTypeName(type),from,to,e->takeoff[0],e->takeoff[1],e->takeoff[2],e->landing[0],e->landing[1],e->landing[2],costMs,validated);
        if(autosave)Autosave();
    }
    return index;
}

static void GuideEdgeChanged(int edge,int moved);
static void SetStrafeState(graphEdge_t *e,int tap,float entrySpeed,const float *entryDir,float exitSpeed,const float *exitDir) {
    e->tap=tap;e->entrySpeed=entrySpeed;e->entryDir[0]=entryDir[0];e->entryDir[1]=entryDir[1];
    e->exitSpeed=exitSpeed;e->exitDir[0]=exitDir[0];e->exitDir[1]=exitDir[1];
}

int Graph_AddStrafeEdge(int from,int to,const vec3_t takeoff,const vec3_t landing,const float *dir,int costMs,int tap,
                        float entrySpeed,const float *entryDir,float exitSpeed,const float *exitDir,int *merged) {
    int e,index;
    *merged=0;
    if(from<0 || to<0 || from>=graph.count || to>=graph.count || from==to)return -1;
    // The same maneuver seen again: one edge, the faster measurement.
    for(e=graph.cell[from].firstEdge;e>=0;e=graph.edge[e].nextFrom) {
        graphEdge_t *x=&graph.edge[e];
        if(x->type!=GRAPH_EDGE_STRAFE || x->to!=to || x->tap!=tap || fabsf(x->entrySpeed-entrySpeed)>GRAPH_STATE_BAND)continue;
        TouchEdge(e);graphGeneration++;
        x->traversals++;*merged=1;
        if(costMs<x->costMs) {
            StrafeNearChanged();
            PathChange(e,x->costMs,0);
            VectorCopy(takeoff,x->takeoff);VectorCopy(landing,x->landing);x->dir[0]=dir[0];x->dir[1]=dir[1];x->costMs=costMs;
            SetStrafeState(x,tap,entrySpeed,entryDir,exitSpeed,exitDir);SaveDirty(e);
            GuideEdgeChanged(e,1);
            if(autosave)Autosave();
        }
        return e;
    }
    index=AddEdgeRecord(GRAPH_EDGE_STRAFE,from,to,takeoff,landing,dir,costMs,1);
    if(index>=0) {
        graphEdge_t *x=&graph.edge[index];
        SetStrafeState(x,tap,entrySpeed,entryDir,exitSpeed,exitDir);
        trap->Print("[GRAPH] edge_added index=%d type=strafe from=%d to=%d takeoff=%.1f,%.1f,%.1f landing=%.1f,%.1f,%.1f cost_ms=%d tap=%d entry_speed=%.1f exit_speed=%.1f\n",
            index,from,to,x->takeoff[0],x->takeoff[1],x->takeoff[2],x->landing[0],x->landing[1],x->landing[2],costMs,tap,entrySpeed,exitSpeed);
        if(autosave)Autosave();
    }
    return index;
}

void Graph_EdgeCost(int edge,int costMs) {
    if(edge<0 || edge>=graph.edgeCount || costMs<=0)return;
    TouchEdge(edge);graphGeneration++;
    if(costMs<graph.edge[edge].costMs)GuideEdgeChanged(edge,0);
    if(costMs<graph.edge[edge].costMs)PathChange(edge,graph.edge[edge].costMs,0);
    else if(costMs>graph.edge[edge].costMs)PathChange(edge,graph.edge[edge].costMs,1);
    if(costMs!=graph.edge[edge].costMs)SaveDirty(edge);
    graph.edge[edge].costMs=costMs;
}

void Graph_EdgeResult(int edge,int success) {
    graphEdge_t *e;
    if(!success) {Graph_EdgeFail(edge,GRAPH_CAUSE_WORLD);return;}
    if(edge<0 || edge>=graph.edgeCount)return;
    TouchEdge(edge);graphGeneration++;
    e=&graph.edge[edge];
    e->traversals++;if(!e->validated) {e->validated=1;SaveDirty(edge);if(autosave)Autosave();}
    trap->Print("[GRAPH] edge_result index=%d type=%s success=%d traversals=%d failures=%d disabled=%d\n",
        edge,EdgeTypeName(e->type),success,e->traversals,e->failures,e->disabled);
}

// bot_moveFailCause (default 1; 0 is the behaviour before failures were attributed: every
// failure charged to the world, a validated edge disabled like any other).
int Graph_FailCause(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveFailCause",value,sizeof(value));
    return value[0]?atoi(value)!=0:1;
}

const char *Graph_CauseName(int cause) {
    return cause==GRAPH_CAUSE_TRANSIENT?"transient":cause==GRAPH_CAUSE_MOVER?"mover":"world";
}

// A transient failure writes nothing and prints no edge_result, so map
// preparation (tools/movement_graph_prepare.py) never counts it as a failure.
void Graph_EdgeFail(int edge,int cause) {
    graphEdge_t *e;int offMs=0;
    if(edge<0 || edge>=graph.edgeCount)return;
    e=&graph.edge[edge];
    if(cause==GRAPH_CAUSE_TRANSIENT) {
        trap->Print("[GRAPH] edge_fail_uncharged index=%d type=%s cause=transient level=%d\n",edge,EdgeTypeName(e->type),level.time);
        return;
    }
    // the reach sets see only off and disabled edges, so only those changes move reachTopology.
    TouchEdge(edge);graphGeneration++;
    if(cause==GRAPH_CAUSE_MOVER)offMs=GRAPH_MOVER_OFF_MS;
    else if(++e->failures>=GRAPH_EDGE_FAILURES) {
        if(e->validated && Graph_FailCause()) {offMs=GRAPH_PROVEN_OFF_MS;e->failures=0;}
        else {e->disabled=1;PathChange(edge,e->costMs,1);reachTopology++;}
    }
    if(offMs) {
        PathChange(edge,e->costMs,1);reachTopology++;
        e->offUntil=Now()+offMs;
        if(!offNext || e->offUntil<offNext)offNext=e->offUntil;
    }
    trap->Print("[GRAPH] edge_result index=%d type=%s success=0 traversals=%d failures=%d disabled=%d cause=%s off_ms=%d\n",
        edge,EdgeTypeName(e->type),e->traversals,e->failures,e->disabled,Graph_CauseName(cause),offMs);
}

// Wall climbs (docs/movement/records/2026-09-24-wall-moves.md). A cell whose
// sample toward an axis neighbour struck solid stands beside a wall; when a
// near-vertical face is within reach that way and a floor lies just beyond
// its top, the run up the wall to that floor is an edge candidate. Found from
// the collision world alone; unvalidated, priced by its rise, and checked by
// rollout from the live state when a plan reaches it, as hints are.
//
// bot_moveClimbs: unset or 1 adds climbs to a graph built from the world; 2
// also adds them to a loaded cache (which changes its identity, so tapes bound
// to it are refused); 0 adds none.
static int ClimbsMode(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveClimbs",value,sizeof(value));
    return value[0]?atoi(value):1;
}
#define GRAPH_CLIMB_REACH 48.0f     // farthest wall face from the cell's origin
#define GRAPH_CLIMB_MIN 48.0f       // lower rises are steps and jumps
#define GRAPH_CLIMB_MAX 440.0f      // measured reach 462 on ctf4 (the record)
#define GRAPH_CLIMB_BEYOND 32.0f    // how far past the face the top floor is sampled
static const float climbStarts[]={GRAPH_CLIMB_MAX+24,380,300,220,140};
static void Climbs(void) {
    static const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    int c,d,k;
    for(c=0;c<graph.count;c++) {
        const graphCell_t *cell=&graph.cell[c];vec3_t p;
        if(!(cell->flags&GRAPH_HAZARD_BLOCKED))continue;
        p[0]=cell->ix*GRAPH_GRID;p[1]=cell->iy*GRAPH_GRID;p[2]=cell->z;
        for(d=0;d<4;d++) {
            trace_t tr;vec3_t to,q,top,bottom,land;float wall,rise,dir[2];int u,cost;
            if(!(cell->refused&(1<<d)))continue;
            VectorSet(to,p[0]+dx[d]*GRAPH_CLIMB_REACH,p[1]+dy[d]*GRAPH_CLIMB_REACH,p[2]);
            trap->Trace(&tr,p,graphMins,graphMaxs,to,ENTITYNUM_NONE,MASK_PLAYERSOLID,qfalse,0,0);
            if(tr.startsolid || tr.allsolid || tr.fraction>=1 || tr.entityNum!=ENTITYNUM_WORLD)continue;
            if(tr.plane.normal[2]<0 || tr.plane.normal[2]>.4f || -(tr.plane.normal[0]*dx[d]+tr.plane.normal[1]*dy[d])<.7f)continue;
            wall=tr.fraction*GRAPH_CLIMB_REACH;
            // Headroom to leave the ground and start up the face.
            VectorCopy(p,to);to[2]+=GRAPH_CLIMB_MIN;
            trap->Trace(&tr,p,graphMins,graphMaxs,to,ENTITYNUM_NONE,MASK_PLAYERSOLID,qfalse,0,0);
            if(tr.fraction<1)continue;
            VectorSet(q,p[0]+dx[d]*(wall+graphMaxs[0]+GRAPH_CLIMB_BEYOND),p[1]+dy[d]*(wall+graphMaxs[1]+GRAPH_CLIMB_BEYOND),0);
            for(k=0;k<(int)ARRAY_LEN(climbStarts);k++) {
                VectorSet(top,q[0],q[1],p[2]+climbStarts[k]);
                VectorSet(bottom,q[0],q[1],p[2]+GRAPH_CLIMB_MIN);
                trap->Trace(&tr,top,graphMins,graphMaxs,bottom,ENTITYNUM_NONE,MASK_PLAYERSOLID,qfalse,0,0);
                if(!tr.startsolid && !tr.allsolid)break;
            }
            if(k==(int)ARRAY_LEN(climbStarts) || tr.fraction>=1 || tr.plane.normal[2]<=.7f || tr.entityNum!=ENTITYNUM_WORLD)continue;
            rise=tr.endpos[2]-p[2];
            if(rise<GRAPH_CLIMB_MIN || rise>GRAPH_CLIMB_MAX)continue;
            // The climb goes up the face itself: the column against it must
            // be open from the floor to above the top. A floor found beyond
            // a support under an overhanging walkway fails here - the climb
            // would meet the walkway's underside (ctf4: 66 of 66 rollouts
            // ended short at three such edges).
            {vec3_t base,up;trace_t col;
            VectorSet(base,p[0]+dx[d]*(wall-1),p[1]+dy[d]*(wall-1),p[2]);
            VectorSet(up,base[0],base[1],p[2]+rise+STEPSIZE);
            trap->Trace(&col,base,graphMins,graphMaxs,up,ENTITYNUM_NONE,MASK_PLAYERSOLID,qfalse,0,0);
            if(col.startsolid || col.allsolid || col.fraction<1)continue;}
            u=Graph_Nearest(tr.endpos,GRAPH_GRID,2*STEPSIZE);
            if(u<0 || u==c)continue;
            land[0]=graph.cell[u].ix*GRAPH_GRID;land[1]=graph.cell[u].iy*GRAPH_GRID;land[2]=graph.cell[u].z;
            dir[0]=dx[d];dir[1]=dy[d];
            // A climb runs at 300 u/s while its animation lasts; the jump,
            // the mount and the step onto the floor add most of a second.
            cost=(int)(rise/300*1000)+800;
            if(AddEdgeRecord(GRAPH_EDGE_CLIMB,c,u,p,land,dir,cost,0)>=0)graph.climbs++;
        }
    }
}

// Lift rides (docs/movement/records/2026-09-24-lifts.md). A lift's cells are
// the ones the fill found on it where it rests (bot2_mover.h). One of them, the
// nearest its middle, is the boarding cell.
// - Up (from the rest position): a bot standing on the boarding cell touches
//   a lift call, waits for it and rides to the lift's other position. The
//   world cells beside the platform there - at the boarding cell's height
//   plus the travel, within two cells of a platform cell - are the exits.
// - Down (to the rest position): a bot standing in a lift call beside the
//   raised platform - a world cell inside the call, near the other position's
//   height - waits for the lift to arrive, steps on, and rides it back when
//   its wait runs out. The world cells beside the resting platform are the
//   exits.
// One edge per exit. The cost is the call's delay, the travel at the lift's
// speed, for a ride down the lift's wait at the far end, and the walk to the
// exit. A lift no call reaches has no ride that way. The edge's tap holds the
// lift's inline model. Rides are executed (bot2_move.c), never rolled out:
// the forecast freezes every mover.
#define GRAPH_LIFT_EXIT_REACH 2     // cells beyond a platform cell an exit may stand
#define GRAPH_LIFT_MARGIN_MS 500    // stopping on the cell and stepping off
#define GRAPH_LIFT_CALL_HEIGHT 64.0f// a call cell stands this near the raised platform's height
static int LiftEdge(int from,int to,const vec3_t takeoff,int model,float ms) {
    vec3_t land;float dir[2],length;int j,e;
    for(j=graph.cell[from].firstEdge;j>=0;j=graph.edge[j].nextFrom)
        if(graph.edge[j].type==GRAPH_EDGE_LIFT && graph.edge[j].to==to)return -1;
    land[0]=graph.cell[to].ix*(float)GRAPH_GRID;land[1]=graph.cell[to].iy*(float)GRAPH_GRID;land[2]=graph.cell[to].z;
    dir[0]=land[0]-takeoff[0];dir[1]=land[1]-takeoff[1];length=sqrtf(dir[0]*dir[0]+dir[1]*dir[1]);
    if(length>1) {dir[0]/=length;dir[1]/=length;} else {dir[0]=1;dir[1]=0;}
    e=AddEdgeRecord(GRAPH_EDGE_LIFT,from,to,takeoff,land,dir,(int)(ms+length/250*1000),0);
    if(e>=0) {graph.edge[e].tap=model;graph.lifts++;}
    return e;
}

// The world cells beside a lift's platform cells, raised by rise: one per
// direction per platform cell, the nearest within the reach.
static int LiftExits(int first,int model,int state,float rise,int *exits,int max) {
    static const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    int k,d,s,n=0,i;
    for(k=first;k<graph.count;k++) {
        const graphCell_t *cell=&graph.cell[k];
        if(cell->mover!=model || cell->moverState!=state)continue;
        for(d=0;d<4;d++)
            for(s=1;s<=GRAPH_LIFT_EXIT_REACH;s++) {
                int x=Lookup(cell->ix+dx[d]*s,cell->iy+dy[d]*s,cell->z+rise,STEPSIZE+.25f);
                if(x<0)continue;
                if(graph.cell[x].mover)break;
                for(i=0;i<n && exits[i]!=x;i++);
                if(i==n && n<max)exits[n++]=x;
                break;
            }
    }
    return n;
}

static void Lifts(void) {
    int c,model,done[MAX_MODELS];
    memset(done,0,sizeof(done));
    for(c=0;c<graph.count;c++) {
        const graphCell_t *first=&graph.cell[c];
        int e,board=-1,up=0,down=0,delay,k,n,exits[64],t;float cx=0,cy=0,count=0,best=1e30f,travel,ms;
        gentity_t *lift;vec3_t offset,p,raised;
        model=first->mover;
        if(!model || model>=MAX_MODELS || done[model])continue;
        done[model]=1;
        if((e=Bot2_MoverEntity(model))<0)continue;
        lift=&g_entities[e];
        if(first->moverState==MOVER_POS1)VectorSubtract(lift->pos2,lift->pos1,offset);
        else if(first->moverState==MOVER_POS2)VectorSubtract(lift->pos1,lift->pos2,offset);
        else continue;
        travel=VectorLength(offset);
        if(travel<STEPSIZE || lift->speed<=0)continue;
        for(k=c;k<graph.count;k++)
            if(graph.cell[k].mover==model && graph.cell[k].moverState==first->moverState) {
                cx+=graph.cell[k].ix*(float)GRAPH_GRID;cy+=graph.cell[k].iy*(float)GRAPH_GRID;count++;
            }
        cx/=count;cy/=count;
        for(k=c;k<graph.count;k++)
            if(graph.cell[k].mover==model && graph.cell[k].moverState==first->moverState) {
                float ddx=graph.cell[k].ix*(float)GRAPH_GRID-cx,ddy=graph.cell[k].iy*(float)GRAPH_GRID-cy;
                if(ddx*ddx+ddy*ddy<best) {best=ddx*ddx+ddy*ddy;board=k;}
            }
        p[0]=graph.cell[board].ix*(float)GRAPH_GRID;p[1]=graph.cell[board].iy*(float)GRAPH_GRID;p[2]=graph.cell[board].z;
        VectorAdd(p,offset,raised);
        // Up: from the boarding cell to the exits beside the raised platform.
        delay=Bot2_MoverBoardDelay(e,p);
        if(delay>=0) {
            ms=delay+travel/lift->speed*1000+GRAPH_LIFT_MARGIN_MS;
            n=LiftExits(c,model,first->moverState,offset[2],exits,ARRAY_LEN(exits));
            for(k=0;k<n;k++)if(LiftEdge(board,exits[k],p,model,ms)>=0)up++;
        }
        // Down: from a world cell inside a call beside the raised platform to
        // the exits beside the resting one. The call cell nearest the raised
        // boarding point is used; the lift waits lift->wait there.
        {
            int call=-1,callDelay=-1;float callBest=1e30f;
            for(t=0;t<graph.count;t++) {
                const graphCell_t *cell=&graph.cell[t];vec3_t q;int d;float ddx,ddy,dist;
                if(cell->mover || fabsf(cell->z-raised[2])>GRAPH_LIFT_CALL_HEIGHT)continue;
                q[0]=cell->ix*(float)GRAPH_GRID;q[1]=cell->iy*(float)GRAPH_GRID;q[2]=cell->z;
                ddx=q[0]-raised[0];ddy=q[1]-raised[1];dist=ddx*ddx+ddy*ddy;
                if(dist>=callBest || dist>512.0f*512.0f)continue;
                if((d=Bot2_MoverBoardDelay(e,q))<0)continue;
                callBest=dist;call=t;callDelay=d;
            }
            if(call>=0) {
                vec3_t q;
                q[0]=graph.cell[call].ix*(float)GRAPH_GRID;q[1]=graph.cell[call].iy*(float)GRAPH_GRID;q[2]=graph.cell[call].z;
                ms=callDelay+2*travel/lift->speed*1000+max(0,lift->wait)+2*GRAPH_LIFT_MARGIN_MS+sqrtf(callBest)/250*1000;
                n=LiftExits(c,model,first->moverState,0,exits,ARRAY_LEN(exits));
                for(k=0;k<n;k++)if(LiftEdge(call,exits[k],q,model,ms)>=0)down++;
            }
        }
        trap->Print("[GRAPH] lift model=%d entity=%d cells=%d board=%d at=%.1f,%.1f,%.1f travel=%.1f speed=%.1f call_ms=%d up=%d down=%d\n",
            model,e,(int)count,board,p[0],p[1],p[2],travel,lift->speed,delay,up,down);
    }
}

// Pads, drops and basic jumps found from the world itself, with no navmesh
// (bot_moveNativeEdges 1; docs/movement/records/2026-09-27-navmesh-off.md).
// They are candidates exactly as the navmesh's hints were: a pad is timed by
// executing it, a drop or jump is rolled out from the live state when the
// route reaches it and refused if no launch lands on its landing's floor.
// - A pad is a trigger_push thrown at its target (AimAtTarget's arc). Its
//   takeoff is the cell nearest the trigger's middle whose standing hull
//   touches the trigger; its landings are where the arc, traced with the hull
//   and sliding off walls as Pmove clips, first meets floor - with no input
//   and under the air strafe toward each of 16 headings, since a vertical
//   pad reaches its ledge only by steering. Landings on the pad's own floor
//   are dropped. A landing is kept only on a cell two cells inside its floor
//   and when the arc steered at it, as the jumppad state steers
//   (Move_PadLanding), comes down there too.
// - A drop leaves a cell toward a refused neighbour whose column is open at
//   the cell's height and has floor more than a step below, within the drop
//   reach: the navmesh's drops end just past the ledge (median 30 units out,
//   32 to 384 down).
// - A jump leaves a cell toward a refused neighbour for the nearest cell
//   along that line, 64 to 192 units out, within a jump's rise above and a
//   jump's fall below, when the hull clears the line at the jump's height and
//   the walk does not already join them within a few links (the navmesh's
//   jumps: 45 to 192 units, 22 to 40 down).
// Liquid, hurt volumes and unreached floor are excluded because the landing
// must be a cell the fill admitted.
// bot_moveNativeEdges: unset or 1 (default) finds these from
// the world and imports none from a navmesh; 0 imports the navmesh's instead.
int Graph_NativeEdges(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveNativeEdges",value,sizeof(value));
    return value[0]?atoi(value)>0:1;
}
// A pad, drop or jump's price until a traversal measures it: the navmesh's
// guess, kept for native edges. The walk is priced at
// about double its measured time, so a pad priced at its true flight was
// chosen over routes the runner strafes faster; the pad's two seconds keep
// it to where it saves real distance.
static int CandidateCostMs(int type,float length) {
    return (int)(length/250*1000)+(type==GRAPH_EDGE_PAD?2000:250);
}
#define GRAPH_PAD_STEP_MS 25
#define GRAPH_PAD_MAX_MS 6000
#define GRAPH_PAD_BUMPS 4
#define GRAPH_PAD_HEADINGS 16
#define GRAPH_PAD_AIR_ACCEL 1.0f    // pm_airaccelerate
#define GRAPH_PAD_OWN_FLOOR 8       // a landing walked to from the pad within this many links is the pad's floor
#define GRAPH_PAD_MARGIN 2          // cells of floor a pad landing keeps on every side
#define GRAPH_PAD_COAST 8.0f        // u/s: a velocity this near the arriving one coasts
#define GRAPH_DROP_REACH 400.0f
#define GRAPH_JUMP_NEAR 64
#define GRAPH_JUMP_FAR 192
#define GRAPH_JUMP_RISE 40.0f       // a tap jump peaks 31.6 above its takeoff; a step's grace on top
#define GRAPH_JUMP_FALL 64.0f       // lower landings are drops
#define GRAPH_JUMP_CLEAR 32.0f      // the flight's height over the line for the clearance trace
#define GRAPH_JUMP_LINKED 4         // walked within this many links: not a gap
static int NativeEdge(int type,int from,int to,const vec3_t takeoff,const vec3_t landing,const float *dir,int costMs) {
    int j;
    for(j=graph.cell[from].firstEdge;j>=0;j=graph.edge[j].nextFrom)
        if(graph.edge[j].type==type && graph.edge[j].to==to)return -1;
    return AddEdgeRecord(type,from,to,takeoff,landing,dir,costMs,0);
}

// The air steering that arrives on a pad edge's landing: the horizontal
// velocity that reaches the landing when the arc comes down to its height,
// less the velocity held now. Returns 1 with the heading to accelerate
// along, 0 when the velocity held already arrives (coast). A steer that
// held forward at the landing all the way overshot it at Force Speed, whose
// air strafe is 1.7 times as strong.
int Graph_PadSteer(const vec3_t pos,const vec3_t vel,const vec3_t landing,float wish[2]) {
    float g=graph.gravity>0?(float)graph.gravity:800.0f,disc,t,need[2],l;
    disc=vel[2]*vel[2]+2*g*(pos[2]-landing[2]);
    t=disc>0?(vel[2]+sqrtf(disc))/g:max(vel[2]/g,0.0f);
    if(t<.05f)t=.05f;
    need[0]=(landing[0]-pos[0])/t-vel[0];need[1]=(landing[1]-pos[1])/t-vel[1];
    l=sqrtf(need[0]*need[0]+need[1]*need[1]);
    if(l<GRAPH_PAD_COAST)return 0;
    wish[0]=need[0]/l;wish[1]=need[1]/l;
    return 1;
}

// One pad arc from the takeoff, stepped as Pmove steps a frame (the height
// moves by the mean of the start and end velocities) and clipped off walls.
// The push is applied again every frame the hull is still inside the trigger,
// as the touch does. Steering is the air strafe (PM_Accelerate at the run's
// wishspeed): none when steer is NULL, a fixed heading when point is 0,
// otherwise toward the point steer, as the jumppad state flies it. Returns 1
// when the arc comes down on floor, with where and after how long.
static int PadArc(const gentity_t *trigger,const vec3_t push,const vec3_t takeoff,const float *steer,int point,float run,vec3_t land,int *flightMs,int pass) {
    float g=graph.gravity>0?(float)graph.gravity:800.0f,dt=GRAPH_PAD_STEP_MS/1000.0f;
    vec3_t pos,vel,next;trace_t tr;int ms,bumps=0;
    VectorCopy(takeoff,pos);VectorCopy(push,vel);
    for(ms=GRAPH_PAD_STEP_MS;ms<=GRAPH_PAD_MAX_MS;ms+=GRAPH_PAD_STEP_MS) {
        float vz;
        if(pos[0]+graphMaxs[0]>=trigger->r.absmin[0] && pos[0]+graphMins[0]<=trigger->r.absmax[0] &&
           pos[1]+graphMaxs[1]>=trigger->r.absmin[1] && pos[1]+graphMins[1]<=trigger->r.absmax[1] &&
           pos[2]+graphMaxs[2]>=trigger->r.absmin[2] && pos[2]+graphMins[2]<=trigger->r.absmax[2])VectorCopy(push,vel);
        else if(steer) {
            float wish[2],current,add,accel=GRAPH_PAD_AIR_ACCEL*dt*run;
            if(point?Graph_PadSteer(pos,vel,steer,wish):(wish[0]=steer[0],wish[1]=steer[1],1)) {
                current=vel[0]*wish[0]+vel[1]*wish[1];add=run-current;
                if(add>0) {if(accel>add)accel=add;vel[0]+=accel*wish[0];vel[1]+=accel*wish[1];}
            }
        }
        vz=vel[2];vel[2]-=g*dt;
        next[0]=pos[0]+vel[0]*dt;next[1]=pos[1]+vel[1]*dt;next[2]=pos[2]+(vz+vel[2])*.5f*dt;
        trap->Trace(&tr,pos,graphMins,graphMaxs,next,pass,MASK_PLAYERSOLID,qfalse,0,0);
        if(tr.startsolid || tr.allsolid)return 0;
        VectorCopy(tr.endpos,pos);
        if(tr.fraction<1) {
            float into;
            if(tr.plane.normal[2]>.7f && vel[2]<=0) {VectorCopy(pos,land);*flightMs=ms;return 1;}
            if(++bumps>GRAPH_PAD_BUMPS)return 0;
            into=DotProduct(vel,tr.plane.normal)*1.001f;
            VectorMA(vel,-into,tr.plane.normal,vel);
        }
    }
    return 0;
}

// Whether a cell is inside its floor rather than at an edge of it: every
// cell within the margin is linked on all four sides, so a landing a little
// off the arc still stands (a pad aimed at a ledge beside a pit, on
// Dantooine, came down short and fell).
static int PadInterior(int cell) {
    int d,k,n;
    for(d=0;d<4;d++)
        for(k=0,n=cell;k<GRAPH_PAD_MARGIN;k++) {
            if(graph.cell[n].refused)return 0;
            if((n=graph.cell[n].link[d])<0)return 0;
        }
    return 1;
}

// Whether a pad edge's landing is reached by the throw from where the bot
// entered the trigger, steered as the jumppad state flies it at this
// wishspeed, with the checks discovery keeps a landing by. Discovery flies
// from the cell nearest the trigger's centre; a bot walks on at an edge, up
// to the trigger's half-width farther from the landing, and without Speed
// the air strafe may not make up the difference (the (160,192) pad on Kejim:
// records/2026-09-28-laps-falls.md). The target is found without
// G_PickTarget, which draws on rand() and would move the world's stream.
float Graph_SpeedFactor(void) {return GRAPH_SPEED_FACTOR;}

// A push's target, the first entity of its name, for discovery and the reach
// check alike. Discovery runs before AimAtTarget, so the trigger's own push
// (s.origin2) is not set yet; it drew G_PickTarget's rand() per pad at every
// build in older versions (no prepared map has a push with two targets).
static gentity_t *PushTarget(const gentity_t *trigger) {
    return trigger->target?G_Find(NULL,FOFS(targetname),trigger->target):NULL;
}

int Graph_PadReachable(const gentity_t *trigger,const vec3_t origin,int edge,float run,int pass) {
    const gentity_t *target;const graphEdge_t *e;vec3_t centre,push,check;
    float g=graph.gravity>0?(float)graph.gravity:800.0f,height,time,dist;int ms,at;
    if(!trigger || edge<0 || edge>=graph.edgeCount || !trigger->target)return 0;
    e=&graph.edge[edge];
    if(!(target=PushTarget(trigger)))return 0;
    VectorAdd(trigger->r.absmin,trigger->r.absmax,centre);VectorScale(centre,.5f,centre);
    height=target->s.origin[2]-centre[2];
    if(height<=0)return 0;
    time=sqrtf(height/(.5f*g));
    push[0]=target->s.origin[0]-centre[0];push[1]=target->s.origin[1]-centre[1];push[2]=0;
    dist=VectorNormalize(push);VectorScale(push,dist/time,push);push[2]=time*g;
    if(!PadArc(trigger,push,origin,e->landing,1,run,check,&ms,pass) || fabsf(check[2]-e->landing[2])>2*STEPSIZE)return 0;
    at=Graph_Nearest(check,GRAPH_GRID+16,2*STEPSIZE);
    return at>=0 && Graph_LinkedWithin(e->to,at,GRAPH_PAD_MARGIN);
}

static void Pads(void) {
    gentity_t *ent=NULL;float g=graph.gravity>0?(float)graph.gravity:800.0f;
    while((ent=G_Find(ent,FOFS(classname),"trigger_push"))!=NULL) {
        gentity_t *target;vec3_t centre,push,takeoff,land,check;
        float height,time,dist,best=1e30f,dir[2],run=graph.speed>0?(float)graph.speed:250.0f;int c,h,from=-1,to=-1,ms,checkMs,e;
        int fell=0,own=0,edge=0,missed=0,kept=0;  // why each heading's landing was or was not kept
        // Linear (4) and relative (16) pushes are not thrown at an apex; one
        // with no touch starts off (SP_trigger_push) until a map script wakes it.
        if((ent->spawnflags&(4|16)) || !ent->touch || (ent->flags&FL_INACTIVE) || !ent->target)continue;
        if(!(target=PushTarget(ent)))continue;
        VectorAdd(ent->r.absmin,ent->r.absmax,centre);VectorScale(centre,.5f,centre);
        height=target->s.origin[2]-centre[2];
        if(height<=0)continue;
        time=sqrtf(height/(.5f*g));
        push[0]=target->s.origin[0]-centre[0];push[1]=target->s.origin[1]-centre[1];push[2]=0;
        dist=VectorNormalize(push);VectorScale(push,dist/time,push);push[2]=time*g;
        for(c=0;c<graph.count;c++) {
            const graphCell_t *cell=&graph.cell[c];float x=cell->ix*(float)GRAPH_GRID,y=cell->iy*(float)GRAPH_GRID,d;
            if(x+graphMaxs[0]<ent->r.absmin[0] || x+graphMins[0]>ent->r.absmax[0] ||
               y+graphMaxs[1]<ent->r.absmin[1] || y+graphMins[1]>ent->r.absmax[1] ||
               cell->z+graphMaxs[2]<ent->r.absmin[2] || cell->z+graphMins[2]>ent->r.absmax[2])continue;
            d=(x-centre[0])*(x-centre[0])+(y-centre[1])*(y-centre[1]);
            if(d<best) {best=d;from=c;}
        }
        if(from<0) {trap->Print("[GRAPH] pad entity=%d refused=no_cell\n",ent->s.number);continue;}
        takeoff[0]=graph.cell[from].ix*(float)GRAPH_GRID;takeoff[1]=graph.cell[from].iy*(float)GRAPH_GRID;takeoff[2]=graph.cell[from].z;
        // Candidate landings: the arc with no input and under a fixed heading
        // (a vertical pad reaches its ledge only by steering). Each is kept
        // only when the arc steered at it, as the jumppad state flies, comes
        // down on the same floor, on a cell inside that floor.
        for(h=-1;h<GRAPH_PAD_HEADINGS;h++) {
            float heading[2];
            heading[0]=cosf(h*2*M_PI/GRAPH_PAD_HEADINGS);heading[1]=sinf(h*2*M_PI/GRAPH_PAD_HEADINGS);
            if(!PadArc(ent,push,takeoff,h>=0?heading:NULL,0,run,land,&ms,ENTITYNUM_NONE)) {fell++;continue;}
            to=Graph_Nearest(land,GRAPH_GRID+16,2*STEPSIZE);
            // Back on the pad's own floor is no edge; so is a lethal fall.
            if(to<0 || graph.cell[to].mover || takeoff[2]-land[2]>GRAPH_DROP_REACH) {fell++;continue;}
            if(to==from || Graph_LinkedWithin(from,to,GRAPH_PAD_OWN_FLOOR)) {own++;continue;}
            if(!PadInterior(to)) {edge++;continue;}
            land[0]=graph.cell[to].ix*(float)GRAPH_GRID;land[1]=graph.cell[to].iy*(float)GRAPH_GRID;land[2]=graph.cell[to].z;
            // The edge serves every level, so the steered arc must come down
            // there at the run's wishspeed and at Force Speed's: at Speed the
            // air strafe is 1.7 times as strong (Metalorn: a pad checked at
            // run speed alone was overshot onto a lower floor at level 2).
            if(!PadArc(ent,push,takeoff,land,1,run,check,&checkMs,ENTITYNUM_NONE) || fabsf(check[2]-land[2])>2*STEPSIZE ||
               !Graph_LinkedWithin(to,Graph_Nearest(check,GRAPH_GRID+16,2*STEPSIZE),GRAPH_PAD_MARGIN) ||
               !PadArc(ent,push,takeoff,land,1,run*GRAPH_SPEED_FACTOR,check,&ms,ENTITYNUM_NONE) || fabsf(check[2]-land[2])>2*STEPSIZE ||
               !Graph_LinkedWithin(to,Graph_Nearest(check,GRAPH_GRID+16,2*STEPSIZE),GRAPH_PAD_MARGIN)) {missed++;continue;}
            checkMs=max(checkMs,ms);
            kept++;
            dir[0]=land[0]-takeoff[0];dir[1]=land[1]-takeoff[1];
            {
                float l=sqrtf(dir[0]*dir[0]+dir[1]*dir[1]);
                if(l>1) {dir[0]/=l;dir[1]/=l;} else {dir[0]=1;dir[1]=0;}
                e=NativeEdge(GRAPH_EDGE_PAD,from,to,takeoff,land,dir,CandidateCostMs(GRAPH_EDGE_PAD,l));
            }
            if(e>=0) {
                graph.nativePads++;
                trap->Print("[GRAPH] pad entity=%d from=%d to=%d steer=%d flight_ms=%d land=%.1f,%.1f,%.1f edge=%d\n",
                    ent->s.number,from,to,h,checkMs,land[0],land[1],land[2],e);
            }
        }
        trap->Print("[GRAPH] pad entity=%d from=%d headings=%d fell=%d own_floor=%d floor_edge=%d steer_missed=%d kept=%d\n",
            ent->s.number,from,GRAPH_PAD_HEADINGS+1,fell,own,edge,missed,kept);
    }
}

static void DropsAndJumps(void) {
    static const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    float g=graph.gravity>0?(float)graph.gravity:800.0f;int c,d,k;
    for(c=0;c<graph.count;c++) {
        const graphCell_t *cell=&graph.cell[c];vec3_t p;
        if(!cell->refused || cell->mover)continue;
        p[0]=cell->ix*(float)GRAPH_GRID;p[1]=cell->iy*(float)GRAPH_GRID;p[2]=cell->z;
        for(d=0;d<4;d++) {
            trace_t tr;vec3_t out,below,land;float dir[2],fall;int u;
            if(!(cell->refused&(1<<d)))continue;
            dir[0]=dx[d];dir[1]=dy[d];
            // Drop: the next column is open at this height and has floor below.
            VectorSet(out,p[0]+dx[d]*GRAPH_GRID,p[1]+dy[d]*GRAPH_GRID,p[2]);
            trap->Trace(&tr,p,graphMins,graphMaxs,out,ENTITYNUM_NONE,MASK_PLAYERSOLID,qfalse,0,0);
            if(!tr.startsolid && !tr.allsolid && tr.fraction>=1) {
                VectorCopy(out,below);below[2]-=GRAPH_DROP_REACH;
                trap->Trace(&tr,out,graphMins,graphMaxs,below,ENTITYNUM_NONE,MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME,qfalse,0,0);
                fall=p[2]-tr.endpos[2];
                if(!tr.startsolid && !tr.allsolid && tr.fraction<1 && tr.plane.normal[2]>.7f &&
                   !(tr.contents&(CONTENTS_LAVA|CONTENTS_SLIME)) && fall>STEPSIZE+.25f &&
                   (u=Graph_Nearest(tr.endpos,GRAPH_GRID,2*STEPSIZE))>=0 && u!=c && !graph.cell[u].mover) {
                    land[0]=graph.cell[u].ix*(float)GRAPH_GRID;land[1]=graph.cell[u].iy*(float)GRAPH_GRID;land[2]=graph.cell[u].z;
                    if(NativeEdge(GRAPH_EDGE_DROP,c,u,p,land,dir,(int)(GRAPH_GRID/250.0f*1000+sqrtf(2*fall/g)*1000))>=0)graph.nativeDrops++;
                }
            }
            // Jump: the nearest cell out along the line within a jump's reach.
            for(k=GRAPH_JUMP_NEAR;k<=GRAPH_JUMP_FAR;k+=GRAPH_GRID) {
                vec3_t top,rise,over;
                VectorSet(top,p[0]+dx[d]*k,p[1]+dy[d]*k,p[2]+GRAPH_JUMP_RISE);
                VectorCopy(top,below);below[2]=p[2]-GRAPH_JUMP_FALL;
                trap->Trace(&tr,top,graphMins,graphMaxs,below,ENTITYNUM_NONE,MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME,qfalse,0,0);
                if(tr.startsolid || tr.allsolid || tr.fraction>=1 || tr.plane.normal[2]<=.7f || (tr.contents&(CONTENTS_LAVA|CONTENTS_SLIME)))continue;
                if((u=Graph_Nearest(tr.endpos,GRAPH_GRID/2,2*STEPSIZE))<0 || u==c || graph.cell[u].mover)continue;
                if(Graph_LinkedWithin(c,u,GRAPH_JUMP_LINKED))break;
                land[0]=graph.cell[u].ix*(float)GRAPH_GRID;land[1]=graph.cell[u].iy*(float)GRAPH_GRID;land[2]=graph.cell[u].z;
                VectorCopy(p,rise);rise[2]=max(p[2],land[2])+GRAPH_JUMP_CLEAR;
                VectorCopy(land,over);over[2]=rise[2];
                trap->Trace(&tr,p,graphMins,graphMaxs,rise,ENTITYNUM_NONE,MASK_PLAYERSOLID,qfalse,0,0);
                if(tr.fraction<1)break;
                trap->Trace(&tr,rise,graphMins,graphMaxs,over,ENTITYNUM_NONE,MASK_PLAYERSOLID,qfalse,0,0);
                if(tr.fraction<1)break;
                if(NativeEdge(GRAPH_EDGE_JUMP,c,u,p,land,dir,(int)(k/250.0f*1000)+250)>=0)graph.nativeJumps++;
                break;
            }
        }
    }
}

// Off-mesh connections from the navmesh, when one is present and
// bot_moveNativeEdges is 0, become edge candidates: pads, drops and basic
// jumps whose ends both bind to cells.
// Wallruns and elevators stay last-resort paths and are not imported.
static void Hints(void) {
    int i,type;float start[3],end[3];
    for(i=0;NavMesh_OffMeshConnection(i,&type,start,end);i++) {
        vec3_t takeoff,landing;float dir[2],length;int from,to,edgeType,cost;
        graph.hints++;
        if(type==OFFMESH_AREA_JUMPPAD)edgeType=GRAPH_EDGE_PAD;
        else if(type==OFFMESH_AREA_JUMP_DROP)edgeType=GRAPH_EDGE_DROP;
        else if(type==OFFMESH_AREA_JUMP_BASIC)edgeType=GRAPH_EDGE_JUMP;
        else continue;
        VectorCopy(start,takeoff);takeoff[2]+=-DEFAULT_MINS_2+.25f;
        VectorCopy(end,landing);landing[2]+=-DEFAULT_MINS_2+.25f;
        from=Graph_Nearest(takeoff,GRAPH_HINT_RADIUS,GRAPH_HINT_HEIGHT);
        to=Graph_Nearest(landing,GRAPH_HINT_RADIUS,GRAPH_HINT_HEIGHT);
        dir[0]=end[0]-start[0];dir[1]=end[1]-start[1];length=sqrtf(dir[0]*dir[0]+dir[1]*dir[1]);
        if(length>1) {dir[0]/=length;dir[1]/=length;} else {dir[0]=1;dir[1]=0;}
        cost=CandidateCostMs(edgeType,length);
        trap->Print("[GRAPH] hint type=%d edge=%s start=%.1f,%.1f,%.1f end=%.1f,%.1f,%.1f from=%d to=%d\n",
            type,EdgeTypeName(edgeType),start[0],start[1],start[2],end[0],end[1],end[2],from,to);
        if(from<0 || to<0 || from==to)continue;
        if(AddEdgeRecord(edgeType,from,to,takeoff,landing,dir,cost,0)>=0)graph.hintsBound++;
    }
}

// ---------------------------------------------------------------------------
// Search. A* by time: links cost the run over 32 units, edges their measured
// or guessed time, and the heuristic is the straight distance at the fastest
// sustained speed, so it never overestimates.
// ---------------------------------------------------------------------------
static void HeapPush(int cell,float key) {
    int i=search.heapCount++,p;
    while(i>0) {
        p=(i-1)>>1;
        if(search.key[p]<=key)break;
        search.heap[i]=search.heap[p];search.key[i]=search.key[p];i=p;
    }
    search.heap[i]=cell;search.key[i]=key;
}

static int HeapPop(void) {
    int top=search.heap[0],i=0,child,last;float lastKey;
    search.heapCount--;
    if(search.heapCount<=0)return top;
    last=search.heap[search.heapCount];lastKey=search.key[search.heapCount];
    for(;;) {
        child=2*i+1;
        if(child>=search.heapCount)break;
        if(child+1<search.heapCount && search.key[child+1]<search.key[child])child++;
        if(search.key[child]>=lastKey)break;
        search.heap[i]=search.heap[child];search.key[i]=search.key[child];i=child;
    }
    search.heap[i]=last;search.key[i]=lastKey;
    return top;
}

// Clearance: link steps from each cell to the nearest boundary cell (one
// with a missing link: a wall, an edge, a refused sample), capped at 255.
// Computed once when the graph is prepared; cells never change afterwards.
// bot_movePlanClearance N (cells, default 6, 0 off) makes the planners charge an arrival
// in a cell with less than N cells of clearance up to one more walk step, so a
// path runs down the middle of open floor instead of along a wall: on dash2
// the path hugged the right wall of a 1,024-unit lane at 100 units, and the
// strafe weave ran into it.
static unsigned char cellClear[GRAPH_MAX_CELLS];
static int clearQueue[GRAPH_MAX_CELLS];
static int clearanceCells;

static void Clearance(void) {
    int i,d,head=0,tail=0;
    pathFull++;
    for(i=0;i<graph.count;i++) {
        int edge=0;
        for(d=0;d<4;d++)if(graph.cell[i].link[d]<0)edge=1;
        cellClear[i]=edge?0:255;
        if(edge)clearQueue[tail++]=i;
    }
    while(head<tail) {
        int c=clearQueue[head++];
        for(d=0;d<4;d++) {
            int n=graph.cell[c].link[d];
            if(n>=0 && cellClear[n]==255 && cellClear[c]<254) {cellClear[n]=cellClear[c]+1;clearQueue[tail++]=n;}
        }
    }
}

static void ClearanceRead(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePlanClearance",value,sizeof(value));
    clearanceCells=value[0]?atoi(value):6;   // default 6; 0 off
    if(clearanceCells<0)clearanceCells=0;
    if(clearanceCells>64)clearanceCells=64;
}

static float ClearancePenalty(int cell) {
    int c;
    if(!clearanceCells || cell<0 || cell>=graph.count)return 0;
    c=cellClear[cell];
    return c>=clearanceCells?0:(float)GRAPH_WALK_MS*(clearanceCells-c)/clearanceCells;
}

int Graph_CellClearance(int cell) {return cell>=0 && cell<graph.count?cellClear[cell]:0;}

// Which cells can still reach a goal cell: a reverse breadth-first search from
// the goal over walk links (both ways by construction) and live edges (from
// their landing back to their takeoff cell). Cached for one goal and one edge
// set; a learned edge only widens the set, so it is rebuilt when the goal or
// the edge count or the graph generation changes. A judge that admits a
// landing no route leaves strands the bot there: on Kejim a fast two-hop flew
// over the blue flag onto the ledge behind it and stood there two minutes.
// Reachability sets for the last few goals (records/2026-10-03-perf-lossless.md): each bot searches
// toward its own goal, and the one-goal cache keyed on graphGeneration was rebuilt (95k edges, a flood of 14k
// cells) on nearly every search. A set is a function of the goal and the topology above, so a slot whose goal,
// cell count, edge count and reachTopology match holds exactly what a rebuild would make.
#define REACH_SLOTS 8
static unsigned char reachSets[REACH_SLOTS][GRAPH_MAX_CELLS];
static struct { int goal,cells,edges;unsigned int topology,used; } reachSlot[REACH_SLOTS];
static unsigned int reachUse;
static int reachQueue[GRAPH_MAX_CELLS],reachHead[GRAPH_MAX_CELLS],reachNext[GRAPH_MAX_EDGES];
// The reverse edge index depends on the topology alone, not the goal: built once per topology and
// shared by every goal's flood. Same key as the sets: cell count, edge count and reachTopology.
static struct { int cells,edges,valid;unsigned int topology; } reachIndex;

int Graph_Reaches(int cell,int goal) {
    int i,d,head=0,tail=0;
    if(cell<0 || cell>=graph.count || goal<0 || goal>=graph.count)return 1;   // nothing to judge by
    OffExpire();
    {
    int k,slot=-1;unsigned char *reachSet;
    for(k=0;k<REACH_SLOTS;k++)
        if(reachSlot[k].used && reachSlot[k].goal==goal && reachSlot[k].cells==graph.count && reachSlot[k].edges==graph.edgeCount &&
           reachSlot[k].topology==reachTopology) {slot=k;break;}
    if(slot>=0) {reachSlot[slot].used=++reachUse;return reachSets[slot][cell];}
    for(k=0,slot=0;k<REACH_SLOTS;k++)if(reachSlot[k].used<reachSlot[slot].used)slot=k;   // least recently used
    reachSet=reachSets[slot];
    {
        BOT2_PERF_BEGIN(perfReach);
        memset(reachSet,0,graph.count);
        if(!reachIndex.valid || reachIndex.cells!=graph.count || reachIndex.edges!=graph.edgeCount ||
           reachIndex.topology!=reachTopology) {
            for(i=0;i<graph.count;i++)reachHead[i]=-1;
            for(i=0;i<graph.edgeCount;i++) {
                int to=graph.edge[i].to;
                reachNext[i]=-1;
                if(Graph_EdgeOff(&graph.edge[i]) || to<0 || to>=graph.count)continue;
                reachNext[i]=reachHead[to];reachHead[to]=i;
            }
            reachIndex.cells=graph.count;reachIndex.edges=graph.edgeCount;reachIndex.topology=reachTopology;reachIndex.valid=1;
        }
        reachSet[goal]=1;reachQueue[tail++]=goal;
        while(head<tail) {
            int c=reachQueue[head++],e;
            for(d=0;d<4;d++) {
                int n=graph.cell[c].link[d];
                if(n>=0 && !reachSet[n]) {reachSet[n]=1;reachQueue[tail++]=n;}
            }
            for(e=reachHead[c];e>=0;e=reachNext[e]) {
                int f=graph.edge[e].from;
                if(f>=0 && f<graph.count && !reachSet[f]) {reachSet[f]=1;reachQueue[tail++]=f;}
            }
        }
        reachSlot[slot].goal=goal;reachSlot[slot].cells=graph.count;reachSlot[slot].edges=graph.edgeCount;
        reachSlot[slot].topology=reachTopology;reachSlot[slot].used=++reachUse;
        BOT2_PERF_END(BOT2_PERF_REACH,perfReach);
    }
    return reachSet[cell];
    }
}

static float HeuristicOf(int cell,int goal) {
    float dx=(graph.cell[cell].ix-graph.cell[goal].ix)*(float)GRAPH_GRID,dy=(graph.cell[cell].iy-graph.cell[goal].iy)*(float)GRAPH_GRID;
    return sqrtf(dx*dx+dy*dy)/GRAPH_HEURISTIC_SPEED*1000;
}

// Lossless speed-ups of the planners (records/2026-10-03-perf-lossless.md): each stores the result of
// a pure function the search used to recompute, so every value, push and pop is the same; a search with them
// returns the identical plan. The heuristic is memoised per goal; a new search (PathSearch, PlanStateSearch) or a new goal
// starts it over.
static struct { unsigned int stamp[GRAPH_MAX_CELLS],now;float value[GRAPH_MAX_CELLS];int goal; } heuristicMemo;
static float Heuristic(int cell,int goal) {
    if(goal!=heuristicMemo.goal || !heuristicMemo.now) {
        if(++heuristicMemo.now==0) {memset(heuristicMemo.stamp,0,sizeof(heuristicMemo.stamp));heuristicMemo.now=1;}
        heuristicMemo.goal=goal;
    }
    if(heuristicMemo.stamp[cell]!=heuristicMemo.now) {
        heuristicMemo.stamp[cell]=heuristicMemo.now;heuristicMemo.value[cell]=HeuristicOf(cell,goal);
    }
    return heuristicMemo.value[cell];
}

// The staged state search's bound (bot_movePlanBound; records/2026-11-09-replan-budget.md, part B). A state search
// still running after N expansions is long. From then on it pauses once the lowest key in its heap passes
// bot_movePlanBoundPct percent of the root's own estimate (armed, arm): until then it may still end near that
// estimate and nothing is spent on it. Paused, the cell search is run (the fallback its caller would need anyway)
// and its route kept here; the state search goes on and stops once its lowest key passes the same percent of that
// route's cost. A cell route through an edge the search avoids bounds nothing (the two searches then answer
// different questions). The caller takes the kept route with Graph_PlanBoundRoute in place of its Graph_Path call:
// the same search under the same bodies and forbidden volume, so the same route. Every later search, and every
// change of the bodies or the forbidden volume, drops it, so only the caller whose state search made it can take
// it. step/stepExpanded: the expansion count at which the lowest key passed each of planBoundPct, for [PLANBOUND]
// (bot_movePlanBoundCheck 1 prints them and stops nothing).
#define PLAN_BOUND_STEPS 5
static const int planBoundPct[PLAN_BOUND_STEPS]={100,125,150,200,300};
static struct {
    int armed;float arm;
    int live,stopped,step,stepExpanded[PLAN_BOUND_STEPS];float limit,at[PLAN_BOUND_STEPS];
    int valid,from,to,count,costMs,cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];
} planBound;

// Bodies (failure attribution): the other live players near a bot
// while it replans. Hulls are 30 units square, so a cell whose origin is
// within GRAPH_BODY_REACH of a body on both axes, at about its height, would
// put the bot inside it.
#define GRAPH_BODY_REACH 40.0f
#define GRAPH_BODY_HEIGHT 64.0f
static struct { int count; vec3_t at[GRAPH_MAX_BODIES]; } bodies;
// BodyBlocks' answer per cell for the current bodies: stamp == now holds it.
static struct { unsigned int stamp[GRAPH_MAX_CELLS],now;unsigned char blocks[GRAPH_MAX_CELLS]; } bodyMemo;
void Graph_BodiesSet(const vec3_t *at,int count) {
    int i;
    bodies.count=count<0?0:min(count,GRAPH_MAX_BODIES);planBound.valid=0;
    for(i=0;i<bodies.count;i++)VectorCopy(at[i],bodies.at[i]);
    if(++bodyMemo.now==0) {memset(bodyMemo.stamp,0,sizeof(bodyMemo.stamp));bodyMemo.now=1;}
}
static int BodyBlocksOf(int cell) {
    const graphCell_t *c;float x,y;int i;
    c=&graph.cell[cell];x=c->ix*(float)GRAPH_GRID;y=c->iy*(float)GRAPH_GRID;
    for(i=0;i<bodies.count;i++)
        if(fabsf(bodies.at[i][0]-x)<GRAPH_BODY_REACH && fabsf(bodies.at[i][1]-y)<GRAPH_BODY_REACH &&
           fabsf(bodies.at[i][2]-c->z)<GRAPH_BODY_HEIGHT)return 1;
    return 0;
}
static int BodyBlocks(int cell) {
    if(!bodies.count)return 0;
    if(!bodyMemo.now)bodyMemo.now=1;
    if(bodyMemo.stamp[cell]!=bodyMemo.now) {bodyMemo.stamp[cell]=bodyMemo.now;bodyMemo.blocks[cell]=(unsigned char)BodyBlocksOf(cell);}
    return bodyMemo.blocks[cell];
}

// The forbidden volume (bot2_graph.h). Per plan, like the bodies: a step into it is
// refused in the search alone, nothing is written to the cells or edges, so other bots' plans never see it.
static struct { int on; vec3_t mins,maxs,arcMins; } forbid;
void Graph_ForbidSet(const float *mins,const float *maxs) {
    forbid.on=mins && maxs;planBound.valid=0;
    if(!forbid.on)return;
    VectorCopy(mins,forbid.mins);VectorCopy(maxs,forbid.maxs);
    VectorCopy(mins,forbid.arcMins);forbid.arcMins[2]-=GRAPH_FORBID_ARC;
}
int Graph_ForbidActive(void) {return forbid.on;}
static int ForbidInside(const float *p) {
    return p[0]>=forbid.mins[0] && p[0]<=forbid.maxs[0] && p[1]>=forbid.mins[1] && p[1]<=forbid.maxs[1] &&
           p[2]>=forbid.mins[2] && p[2]<=forbid.maxs[2];
}
// A segment from a point outside the box that meets [mins, forbid.maxs]: the bounds first (six compares), then
// the slab test. A segment from a point inside the box may leave it (a root inside: the bot walks out).
static int ForbidSegment(const float *a,const float *b,const float *mins) {
    vec3_t below;int i;
    if(ForbidInside(a))return 0;
    for(i=0;i<3;i++)if((a[i]<mins[i] && b[i]<mins[i]) || (a[i]>forbid.maxs[i] && b[i]>forbid.maxs[i]))return 0;
    // Bot2_SegmentEntersBox exempts a start inside the box it is given; a takeoff under the box but inside its
    // arc allowance is not inside the box, so it is tested from just below the lowered box instead.
    VectorCopy(a,below);
    if(below[0]>=mins[0] && below[0]<=forbid.maxs[0] && below[1]>=mins[1] && below[1]<=forbid.maxs[1] &&
       below[2]>=mins[2] && below[2]<=forbid.maxs[2])return 1;   // inside the arc allowance, under the box: the arc meets it
    return Bot2_SegmentEntersBox(a,b,mins,forbid.maxs);
}
int Graph_ForbidBlocks(int from,int to,int edge) {
    vec3_t a,b;const graphCell_t *c;
    if(!forbid.on || from<0 || to<0 || from>=graph.count || to>=graph.count)return 0;
    c=&graph.cell[from];VectorSet(a,c->ix*(float)GRAPH_GRID,c->iy*(float)GRAPH_GRID,c->z);
    // Each segment is exempt only when it starts inside the box (cfd9b265 exempted a whole edge from a source
    // inside it, and a route leaving the box could fly back through it: 0.7 a Kejim world in the third arm).
    if(edge>=0 && edge<graph.edgeCount) {
        const graphEdge_t *e=&graph.edge[edge];
        return ForbidSegment(a,e->takeoff,forbid.mins) || ForbidSegment(e->takeoff,e->landing,forbid.arcMins);
    }
    c=&graph.cell[to];VectorSet(b,c->ix*(float)GRAPH_GRID,c->iy*(float)GRAPH_GRID,c->z);
    return ForbidSegment(a,b,forbid.mins);
}

// The current bodies leave both searches no way into the goal (option 2c,
// records/2026-10-06-frame-cost.md): a defender's goal on the carrier it chases
// is ringed by the carrier's own body, and the search explored everything
// reachable before failing (94-162 ms on Kejim). Exact and conservative: every
// cell with a walk or diagonal arc into the goal (any cell, not only the grid
// neighbours) is body-blocked and not the
// root, and no edge of any state lands on the goal (strafe edges are taken from
// neighbouring cells, so their sources are not tested).
int Graph_BodiesEncloseGoal(int from,int goal) {
    const graphCell_t *g;int i,d,near=0;float gx,gy;
    if(!bodies.count || from<0 || goal<0 || from>=graph.count || goal>=graph.count || from==goal)return 0;
    g=&graph.cell[goal];gx=g->ix*(float)GRAPH_GRID;gy=g->iy*(float)GRAPH_GRID;
    for(i=0;i<bodies.count && !near;i++)
        near=fabsf(bodies.at[i][0]-gx)<GRAPH_GRID+GRAPH_BODY_REACH && fabsf(bodies.at[i][1]-gy)<GRAPH_GRID+GRAPH_BODY_REACH &&
             fabsf(bodies.at[i][2]-g->z)<GRAPH_BODY_HEIGHT+256;
    if(!near)return 0;
    for(i=0;i<graph.count;i++) {
        const graphCell_t *c=&graph.cell[i];int into=0;
        if(i==goal)continue;
        for(d=0;d<4 && !into;d++) {
            int a=c->link[d],b=c->link[(d+1)&3];
            into=a==goal || (a>=0 && b>=0 && graph.cell[a].link[(d+1)&3]==goal && graph.cell[b].link[d]==goal);
        }
        if(into && (i==from || !BodyBlocks(i)))return 0;
    }
    for(i=0;i<graph.edgeCount;i++)if(graph.edge[i].to==goal)return 0;
    return 1;
}

static void Touch(int cell) {
    if(search.cellStamp[cell]!=search.stamp) {
        search.cellStamp[cell]=search.stamp;search.state[cell]=0;search.g[cell]=1e30f;search.parent[cell]=-1;search.parentEdge[cell]=-1;
    }
}

static void Relax(int cell,int next,int edge,float cost,int goal) {
    float g=search.g[cell]+cost+ClearancePenalty(next);
    // Guidance, when a corridor is loaded: the goal cell is always reachable
    // (the corridor ends at it), every other cell must be inside.
    if(next!=goal && (!Graph_CorridorAdmits(next) || BodyBlocks(next)))return;
    if(!Graph_CellStands(next))return;
    if(forbid.on && Graph_ForbidBlocks(cell,next,edge))return;   // the plan's forbidden volume
    Touch(next);
    if(search.state[next]==2 || g>=search.g[next])return;
    search.g[next]=g;search.parent[next]=cell;search.parentEdge[next]=edge;search.state[next]=1;
    HeapPush(next,g+Heuristic(next,goal));
}

// A route longer than the caller's array: every search returned 0, the answer for
// unreachable, so a goal more than GRAPH_MAX_PATH cells away read as no route (the reach tests, the defenders' route
// cost) or replanned every MOVE_PATH_RETRY. Now the first `max` cells come back, the cost is the whole route's, and
// the bot replans on the way. path_truncated is printed once a map (no prepared map has a route over 170 cells).
static int pathTruncatedLogged;
static void PathTruncated(const char *search,int from,int to,int count,int max) {
    if(pathTruncatedLogged)return;
    pathTruncatedLogged=1;
    trap->Print("[GRAPH] path_truncated search=%s from=%d to=%d cells=%d kept=%d\n",search,from,to,count,max);
}

static int PathSearch(int from,int to,int *cells,int *edges,int max,int *costMs) {
    int cell,d,e,count=0,i,expanded=0;
    ClearanceRead();planBound.valid=0;
    if(from<0 || to<0 || from>=graph.count || to>=graph.count || max<1)return 0;
    search.stamp++;search.heapCount=0;
    if(search.stamp==0) {memset(search.cellStamp,0,sizeof(search.cellStamp));search.stamp=1;}
    heuristicMemo.goal=-1;   // a new search: the heuristic memo starts over (the graph may have been reloaded)
    Touch(from);search.g[from]=0;search.state[from]=1;HeapPush(from,Heuristic(from,to));
    while(search.heapCount>0) {
        cell=HeapPop();
        if(search.state[cell]==2)continue;
        search.state[cell]=2;expanded++;bot2_meterExpanded++;bot2_meterExpandedCell++;if(!bot2_meterInReplan)bot2_meterExpandedCellOther++;
        if(cell==to)break;
        for(d=0;d<4;d++)if(graph.cell[cell].link[d]>=0)Relax(cell,graph.cell[cell].link[d],-1,GRAPH_WALK_MS,to);
        // A diagonal step is admitted when both orthogonal routes around the
        // square are linked, so it crosses only validated floor; the walk
        // then cuts the corner as it cuts every other.
        for(d=0;d<4;d++) {
            int a=graph.cell[cell].link[d],b=graph.cell[cell].link[(d+1)&3];
            if(a>=0 && b>=0 && graph.cell[a].link[(d+1)&3]>=0 && graph.cell[a].link[(d+1)&3]==graph.cell[b].link[d])
                Relax(cell,graph.cell[a].link[(d+1)&3],-1,GRAPH_DIAGONAL_MS,to);
        }
        for(e=graph.cell[cell].firstEdge;e>=0;e=graph.edge[e].nextFrom)
            if(!Graph_EdgeOff(&graph.edge[e]))Relax(cell,graph.edge[e].to,e,(float)graph.edge[e].costMs,to);
    }
    if(search.cellStamp[to]!=search.stamp || search.state[to]!=2)return 0;
    for(cell=to;cell>=0;cell=search.parent[cell])count++;
    for(cell=to,i=count-1;cell>=0;cell=search.parent[cell],i--)if(i<max) {cells[i]=cell;edges[i]=search.parentEdge[cell];}
    if(costMs)*costMs=(int)search.g[to];
    if(count>max) {PathTruncated("cell",from,to,count,max);return max;}
    return count;
}

// ---------------------------------------------------------------------------
// The state search (phase 3, step 2). A strafe run is a chain: the landing
// speed and heading of one hop are the launch state of the next, so the
// value of a hop depends on the state it leaves for the rest of the trip.
// The search state is the cell plus the dimensions the rollouts showed
// matter: speed (banded), heading (an octant, kept only on a landing frame,
// where it decides the next tap's entry), whether the state is a landing
// frame, and the Speed window left. Walk links are priced by the walk's own
// law from the state's speed, so a fast landing is worth its next few
// cells; a strafe edge is admitted only from a state inside its measured
// entry envelope and leaves its measured exit state; every other edge
// leaves the walk's state. A* by time, the same heuristic as the cell
// search. Entries live in a hash table in BSS and the search reports when
// it overflows, so the caller can fall back to the cell search.
// ---------------------------------------------------------------------------
typedef struct {
    unsigned int key;
    float g;
    int parent,parentEdge,cell;
    graphState_t state;
    unsigned char closed;
    unsigned char taken;                // the required edge is on the path to this entry
} graphStateEntry_t;

typedef struct {
    graphStateEntry_t entry[GRAPH_STATE_MAX];
    // Open addressing: index is the entry index + 1, 0 empty. A slot is live
    // only when its stamp is the search's: clearing the table is one increment
    // instead of a 1 MiB memset per call. The probe sequence and the entries
    // are exactly those of a zeroed table. The slot also holds its entry's key
    // (set once, at creation, as the entry's), so a probe reads one record
    // instead of three arrays (records/2026-10-03-perf-lossless.md).
    struct { unsigned int stamp;int index;unsigned int key; } slot[GRAPH_STATE_MAX*2];
    unsigned int stamp;
    int count,overflow;
    // The heap's key beside its entry, one record a node: the same comparisons in the same order.
    struct { float key;int entry; } heap[GRAPH_STATE_MAX*2];int heapCount;
    int require;                        // the edge a route must take, -1 none (one search's directive)
    // The search in progress, so it can run in parts (StateBegin, StateRun, StateResult).
    int from,to,goalEntry,expanded,done;
    int avoid[GRAPH_STATE_AVOID],avoidCount;
} graphStateSearch_t;

// The table every search runs in. A search split over frames (Graph_PlanSplit*,
// bot_movePlanSplit) keeps its own, so the searches between its parts run
// in the main one untouched; the code below reads whichever is current.
static graphStateSearch_t stateSearchMain;
static graphStateSearch_t *stateSearchAt=&stateSearchMain;
#define stateSearch (*stateSearchAt)

// Joins (bot_moveRerouteJoin, records/2026-10-06-frame-cost.md, "Reroute searches that rejoin"): the
// cells of a route being left, each with the time that route has left from it. While set, a search that reaches one
// prices it as the goal reached through it - its time there plus the time left - and ends when nothing on its heap
// could beat the best such total; the goal itself is the join with no time left. Ignored by a search with a required
// edge. With a slack it ends once its lowest key is within that many ms of the best total (proof traded for work).
// Graph_PlanJoined names the cell the last search ended on (-1 when it reached the goal or nothing).
static struct {
    unsigned int stamp[GRAPH_MAX_CELLS],now;float rest[GRAPH_MAX_CELLS];
    int active,best,joined;float bestCost,slack;
} stateJoin;
// The g of each cell of the last route StateResult read.
static struct { float g[GRAPH_MAX_PATH];int count; } stateTimes;

void Graph_PlanJoinSet(const int *cells,const float *rest,int count,float slack) {
    int i;
    if(++stateJoin.now==0) {memset(stateJoin.stamp,0,sizeof(stateJoin.stamp));stateJoin.now=1;}
    for(i=0;i<count;i++) {
        int c=cells[i];
        if(c<0 || c>=graph.count)continue;
        if(stateJoin.stamp[c]!=stateJoin.now || rest[i]<stateJoin.rest[c]) {
            stateJoin.stamp[c]=stateJoin.now;stateJoin.rest[c]=rest[i];
        }
    }
    stateJoin.active=count>0;stateJoin.slack=slack;
    // A search that returns before StateBegin (a cell out of range) reports no join, not the last search's.
    stateJoin.best=-1;stateJoin.joined=-1;
}
void Graph_PlanJoinClear(void) {stateJoin.active=0;}
int Graph_PlanJoined(void) {return stateJoin.joined;}
int Graph_PlanStateTimes(float *times,int max) {
    if(stateTimes.count>max)return 0;
    memcpy(times,stateTimes.g,sizeof(float)*stateTimes.count);
    return stateTimes.count;
}

static float StateWish(const graphState_t *s) {
    return graph.speed*(s->speedMs>0?GRAPH_SPEED_FACTOR:1.0f);
}

static int StateOctant(const float *dir) {
    float a=atan2f(dir[1],dir[0])*180/M_PI+22.5f;
    int o=(int)floorf(a/45.0f);
    return ((o%8)+8)%8;
}

// Bit 26 is the required-edge bit: set on the successor of the required edge,
// inherited by every later relaxation, and needed at the goal. It is always 0
// when no edge is required, so the key, the table and the search are then
// bit-identical to a search without the directive.
static unsigned int StateKey(int cell,const graphState_t *s,int taken) {
    unsigned int band=(unsigned int)min(31,(int)(max(0.0f,s->speed)/32));
    unsigned int octant=s->tap?(unsigned int)StateOctant(s->dir):0;
    return 1u|((unsigned int)cell<<1)|(band<<16)|((s->tap?1u:0u)<<21)|(octant<<22)|((s->speedMs>0?1u:0u)<<25)|
           ((taken?1u:0u)<<26);
}

// The walk over a link from a speed: friction and the run's acceleration per
// command frame, then the distance covered. Returns the time in ms and the
// speed the walk leaves the link with.
static float StateWalkOf(float speed,float distance,float wish,float *exitSpeed) {
    float v=speed,travelled=0,ms=0,dt=GRAPH_STATE_STEP_MS*.001f;int frames;
    for(frames=0;frames<400;frames++) {
        float drop=max(v,GRAPH_STOP_SPEED)*GRAPH_FRICTION*dt,step;
        v=max(0.0f,v-drop);
        if(v<wish)v=min(wish,v+GRAPH_ACCEL*wish*dt);
        step=v*dt;
        if(travelled+step>=distance) {ms+=(distance-travelled)/step*GRAPH_STATE_STEP_MS;travelled=distance;break;}
        travelled+=step;ms+=GRAPH_STATE_STEP_MS;
    }
    *exitSpeed=v;
    return ms;
}

// StateWalkOf is a pure function of (speed, distance, wish); its results are kept by the bits of those inputs,
// so a hit returns the very floats a fresh evaluation would. Most walks start at exactly the wish speed.
#define STATE_WALK_MEMO 1024
static struct { unsigned int speed[STATE_WALK_MEMO],distance[STATE_WALK_MEMO],wish[STATE_WALK_MEMO];
                float ms[STATE_WALK_MEMO],exit[STATE_WALK_MEMO];unsigned char used[STATE_WALK_MEMO]; } walkMemo;
static float StateWalk(float speed,float distance,const graphState_t *s,float *exitSpeed) {
    float wish=StateWish(s);unsigned int a,b,c,h;
    memcpy(&a,&speed,4);memcpy(&b,&distance,4);memcpy(&c,&wish,4);
    h=(a*2654435761u^b*2246822519u^c*3266489917u)>>22;
    if(walkMemo.used[h] && walkMemo.speed[h]==a && walkMemo.distance[h]==b && walkMemo.wish[h]==c) {
        *exitSpeed=walkMemo.exit[h];return walkMemo.ms[h];
    }
    walkMemo.ms[h]=StateWalkOf(speed,distance,wish,&walkMemo.exit[h]);
    walkMemo.speed[h]=a;walkMemo.distance[h]=b;walkMemo.wish[h]=c;walkMemo.used[h]=1;
    *exitSpeed=walkMemo.exit[h];return walkMemo.ms[h];
}

static void StateHeapPush(int entry,float key) {
    int i,p;
    if(stateSearch.heapCount>=GRAPH_STATE_MAX*2) {stateSearch.overflow=1;return;}
    i=stateSearch.heapCount++;
    while(i>0) {
        p=(i-1)>>1;
        if(stateSearch.heap[p].key<=key)break;
        stateSearch.heap[i]=stateSearch.heap[p];i=p;
    }
    stateSearch.heap[i].entry=entry;stateSearch.heap[i].key=key;
}

static int StateHeapPop(void) {
    int top=stateSearch.heap[0].entry,i=0,child,last;float lastKey;
    stateSearch.heapCount--;
    if(stateSearch.heapCount<=0)return top;
    last=stateSearch.heap[stateSearch.heapCount].entry;lastKey=stateSearch.heap[stateSearch.heapCount].key;
    for(;;) {
        child=2*i+1;
        if(child>=stateSearch.heapCount)break;
        if(child+1<stateSearch.heapCount && stateSearch.heap[child+1].key<stateSearch.heap[child].key)child++;
        if(stateSearch.heap[child].key>=lastKey)break;
        stateSearch.heap[i]=stateSearch.heap[child];i=child;
    }
    stateSearch.heap[i].entry=last;stateSearch.heap[i].key=lastKey;
    return top;
}

// The entry for a key, created untouched when absent; -1 when the table is full.
// The slot is the product's top bits: its low bits depend only on the key's low bits (the cell and
// two speed-band bits), so every heading, tap and Speed state of a cell probed from one run of slots. Entries are
// numbered by creation, not by slot, so the search is the same.
#define GRAPH_STATE_SLOT_BITS 18
typedef char graphStateSlotBitsMatch_t[(1<<GRAPH_STATE_SLOT_BITS)==GRAPH_STATE_MAX*2?1:-1];
static int StateEntry(unsigned int key,int cell,const graphState_t *s,int taken,int *created) {
    unsigned int h=(key*2654435761u)>>(32-GRAPH_STATE_SLOT_BITS);
    *created=0;
    for(;;) {
        int i=stateSearch.slot[h].stamp==stateSearch.stamp?stateSearch.slot[h].index:0;
        if(!i) {
            graphStateEntry_t *e;
            if(stateSearch.count>=GRAPH_STATE_MAX) {stateSearch.overflow=1;return -1;}
            e=&stateSearch.entry[stateSearch.count];
            e->key=key;e->g=1e30f;e->parent=-1;e->parentEdge=-1;e->cell=cell;e->state=*s;e->closed=0;e->taken=(unsigned char)taken;
            stateSearch.slot[h].index=stateSearch.count+1;stateSearch.slot[h].stamp=stateSearch.stamp;stateSearch.slot[h].key=key;*created=1;
            return stateSearch.count++;
        }
        if(stateSearch.slot[h].key==key)return i-1;
        h=(h+1)&(GRAPH_STATE_MAX*2-1);
    }
}

static float StateHeuristic(int cell,const graphState_t *s,int goal);
static void CacheBase(char *path,int size);
#define GUIDE_UNREACHED 1e9f    // a guidance table's time for a cell no relaxed route leaves
static void StateRelax(int from,int cell,const graphState_t *s,int edge,float cost,int goal) {
    int created,i,taken=stateSearch.entry[from].taken || (edge>=0 && edge==stateSearch.require);
    graphStateEntry_t *e;float g=stateSearch.entry[from].g+cost+ClearancePenalty(cell),h;
    if(cell!=goal && (!Graph_CorridorAdmits(cell) || BodyBlocks(cell)))return;
    if(!Graph_CellStands(cell))return;
    if(forbid.on && Graph_ForbidBlocks(stateSearch.entry[from].cell,cell,edge))return;
    i=StateEntry(StateKey(cell,s,taken),cell,s,taken,&created);
    if(i<0)return;
    e=&stateSearch.entry[i];
    if(e->closed || g>=e->g)return;
    e->g=g;e->parent=from;e->parentEdge=edge;e->state=*s;
    h=StateHeuristic(cell,s,goal);
    if(h>=GUIDE_UNREACHED)return;   // guided: even the relaxed graph leaves this cell no route to the goal
    StateHeapPush(i,g+h);
}

static int CellReach(int cell,const vec3_t point,float radius,float height) {
    const graphCell_t *c=&graph.cell[cell];
    float dx=c->ix*(float)GRAPH_GRID-point[0],dy=c->iy*(float)GRAPH_GRID-point[1];
    return fabsf(c->z-point[2])<=height && dx*dx+dy*dy<=radius*radius;
}

// cos(GRAPH_STATE_CONE), evaluated once by the expression the admission test
// used to evaluate on every call, so the value compared is the same float.
static float StateConeCos(void) {
    static float cone;static int ready;
    if(!ready) {cone=cosf(GRAPH_STATE_CONE*M_PI/180);ready=1;}
    return cone;
}

int Graph_StrafeAdmits(int edge,int cell,const graphState_t *state) {
    const graphEdge_t *e;
    if(edge<0 || edge>=graph.edgeCount || cell<0 || cell>=graph.count)return 0;
    e=&graph.edge[edge];
    if(e->type!=GRAPH_EDGE_STRAFE || Graph_EdgeOff(e))return 0;
    if(!CellReach(cell,e->takeoff,GRAPH_STATE_REACH,2*STEPSIZE))return 0;
    if(e->tap && !state->tap)return 0;
    // Every edge is priced from its measured launch, excluding any run-up.
    // A normal-floor tap does not buy free acceleration or a heading change.
    if(fabsf(state->speed-e->entrySpeed)>GRAPH_STATE_BAND)return 0;
    if(state->dir[0]*e->entryDir[0]+state->dir[1]*e->entryDir[1]<StateConeCos())return 0;
    return 1;
}

static struct { unsigned int stamp[GRAPH_MAX_EDGES],now; } avoidMark;
// Marks this search's avoided edges (StateAvoided then reads one stamp).
static void StateAvoidMark(const int *avoid,int avoidCount) {
    int i;
    if(++avoidMark.now==0) {memset(avoidMark.stamp,0,sizeof(avoidMark.stamp));avoidMark.now=1;}
    for(i=0;i<avoidCount;i++)if(avoid[i]>=0 && avoid[i]<GRAPH_MAX_EDGES)avoidMark.stamp[avoid[i]]=avoidMark.now;
}
static int StateAvoided(int edge,const int *avoid,int avoidCount) {
    (void)avoid;(void)avoidCount;
    return edge>=0 && edge<GRAPH_MAX_EDGES && avoidMark.stamp[edge]==avoidMark.now;
}

// Relax one strafe edge from an entry when the state admits it. The caller
// has established the edge's kind and its takeoff's reach from the cell.
static void StateStrafeEdge(int from,int e,float cone,const int *avoid,int avoidCount,int goal) {
    const graphStateEntry_t *x=&stateSearch.entry[from];
    const graphEdge_t *edge=&graph.edge[e];graphState_t next;
    // Graph_StrafeAdmits and StateAvoided, inlined and reordered: the
    // tests and the values they compare are theirs exactly.
    if(Graph_EdgeOff(edge))return;
    if(edge->tap && !x->state.tap)return;
    if(fabsf(x->state.speed-edge->entrySpeed)>GRAPH_STATE_BAND)return;
    if(x->state.dir[0]*edge->entryDir[0]+x->state.dir[1]*edge->entryDir[1]<cone)return;
    if(StateAvoided(e,avoid,avoidCount))return;
    next.speed=edge->exitSpeed;next.dir[0]=edge->exitDir[0];next.dir[1]=edge->exitDir[1];next.tap=1;
    next.speedMs=max(0,x->state.speedMs-edge->costMs);
    StateRelax(from,edge->to,&next,e,(float)edge->costMs,goal);
}

// Relax the strafe edges leaving a cell that the state admits.
static void StateStrafeEdges(int from,int at,int owner,const int *avoid,int avoidCount,int goal) {
    int e;float cone=StateConeCos();
    for(e=graph.cell[owner].firstEdge;e>=0;e=graph.edge[e].nextFrom) {
        const graphEdge_t *edge=&graph.edge[e];
        if(edge->type!=GRAPH_EDGE_STRAFE)continue;
        if(!CellReach(at,edge->takeoff,GRAPH_STATE_REACH,2*STEPSIZE))continue;
        StateStrafeEdge(from,e,cone,avoid,avoidCount,goal);
    }
}

// The strafe edges an expansion of a cell can admit, independent of the
// state: of the cell and its eight neighbours, in the order the expansion
// visits them (the cell, then per direction the neighbour and the next one
// around), each owner's chain in its own order, only strafe edges whose
// takeoff the cell reaches. The search relaxes exactly the edges, and in
// exactly the order, that the chain walk did, so the heap sees the same
// pushes. Built lazily per cell and dropped whole when a link, a chain or a
// takeoff changes. A cell whose list does not fit is walked as before.
#define GRAPH_STRAFE_NEAR_MAX (1<<20)
static struct {
    unsigned int built;                 // the generation the arena holds; 0 none
    unsigned int stamp[GRAPH_MAX_CELLS];// the generation a cell's list was made in
    int first[GRAPH_MAX_CELLS],count[GRAPH_MAX_CELLS];  // count -1: did not fit
    int edge[GRAPH_STRAFE_NEAR_MAX];
    // Copies of each listed edge's tap, entry speed and entry direction, the fields the per-state filter tests:
    // the arena is dropped (StrafeNearChanged) whenever SetStrafeState or a new edge could change them, so the
    // copies are the edge's own bits and the filter reads one contiguous record instead of the 9.5 MB edge array.
    struct { float speed,dir[2];int tap; } entry[GRAPH_STRAFE_NEAR_MAX];
    int used;
} strafeNear;
// Perf counters: cells that have had a list since the map loaded, so a rebuild after a whole-arena drop is told
// from a first build.
static unsigned char strafeNearEver[GRAPH_MAX_CELLS];

// Perf counters: links whose opposite does not point back (link[d] -> n but n's link[(d+2)&3] is not the cell),
// and of those, links that n does not return in any direction. A local near-list invalidation needs both 0.
void Graph_PerfLinks(int *cells,int *opposite,int *any) {
    int i,d,k;
    *cells=graph.count;*opposite=*any=0;
    for(i=0;i<graph.count;i++)for(d=0;d<4;d++) {
        int n=graph.cell[i].link[d];
        if(n<0)continue;
        if(n>=graph.count) {(*opposite)++;(*any)++;continue;}
        if(graph.cell[n].link[(d+2)&3]==i)continue;
        (*opposite)++;
        for(k=0;k<4 && graph.cell[n].link[k]!=i;k++) {}
        if(k==4)(*any)++;
    }
}

static int StrafeNearAppend(int at,int owner) {
    int e;
    for(e=graph.cell[owner].firstEdge;e>=0;e=graph.edge[e].nextFrom) {
        const graphEdge_t *edge=&graph.edge[e];
        if(edge->type!=GRAPH_EDGE_STRAFE || !CellReach(at,edge->takeoff,GRAPH_STATE_REACH,2*STEPSIZE))continue;
        if(strafeNear.used>=GRAPH_STRAFE_NEAR_MAX)return 0;
        strafeNear.entry[strafeNear.used].speed=edge->entrySpeed;strafeNear.entry[strafeNear.used].tap=edge->tap;
        strafeNear.entry[strafeNear.used].dir[0]=edge->entryDir[0];strafeNear.entry[strafeNear.used].dir[1]=edge->entryDir[1];
        strafeNear.edge[strafeNear.used++]=e;
    }
    return 1;
}

// A cell's list: its count, the first index through *first; -1 when it did not fit.
static int StrafeNear(int cell,int *first) {
    if(!strafeNearGeneration)strafeNearGeneration=1;
    if(strafeNear.built!=strafeNearGeneration) {
        if(strafeNear.built>strafeNearGeneration)memset(strafeNear.stamp,0,sizeof(strafeNear.stamp)); // wrapped
        strafeNear.built=strafeNearGeneration;strafeNear.used=0;
        BOT2_PERF_N(BOT2_PERF_NEAR_DROP);
    }
    if(strafeNear.stamp[cell]!=strafeNear.built) {
        int start=strafeNear.used,ok,d;
        BOT2_PERF_BEGIN(perfNear);
        if(bot2_perfOn && strafeNearEver[cell])bot2_perf.n[BOT2_PERF_NEAR_REBUILD]++;
        strafeNearEver[cell]=1;
        ok=StrafeNearAppend(cell,cell);
        for(d=0;d<4 && ok;d++) {
            int a=graph.cell[cell].link[d],b;
            if(a<0)continue;
            ok=StrafeNearAppend(cell,a);
            b=graph.cell[a].link[(d+1)&3];
            if(ok && b>=0)ok=StrafeNearAppend(cell,b);
        }
        if(!ok)strafeNear.used=start;
        strafeNear.first[cell]=start;strafeNear.count[cell]=ok?strafeNear.used-start:-1;
        strafeNear.stamp[cell]=strafeNear.built;
        BOT2_PERF_END(BOT2_PERF_NEAR_BUILD,perfNear);
    }
    *first=strafeNear.first[cell];
    return strafeNear.count[cell];
}

static void StateLink(int from,int next,float distance,const float *dir,int goal) {
    const graphStateEntry_t *x=&stateSearch.entry[from];graphState_t s;float ms;
    ms=StateWalk(x->state.speed,distance,&x->state,&s.speed);
    s.dir[0]=dir[0];s.dir[1]=dir[1];s.tap=0;s.speedMs=max(0,x->state.speedMs-(int)ms);
    StateRelax(from,next,&s,-1,ms,goal);
}

// ---------------------------------------------------------------------------
// Planner guidance (bot_movePlanGuide, default 1, with bot_movePlanGuideMax 1;
// records/2026-10-06-frame-cost.md and 2026-10-30-hop-room-offline.md). The state search's
// heuristic as a per-goal table of time to the goal over a relaxation of the search's own graph, in place of the
// straight line at GRAPH_HEURISTIC_SPEED, which is about half the time of a route walked mostly at the run's speed
// (Kejim's cross-map replans expand about 28,000 states). The table is a lower bound on the search's own cost and
// consistent, so the search still returns a cheapest route by its own costs:
//  - a walk link of length d at wish w: friction takes a speed above w down by v*F*dt a frame, so the distance a link
//    covers above w is at most (v0-v1)/F; with the potential phi(v)=max(0,v-w)/(F*w), t >= d/w - phi(v0) + phi(v1);
//  - a strafe edge costs costMs and takes the speed from at least entrySpeed-BAND to exitSpeed;
//  - every other edge costs costMs and leaves the wish speed, phi 0.
// T(cell), the shortest time over the reduced costs (walk d/w; strafe costMs+phi(entry-BAND)-phi(exit); other costMs),
// gives h(state)=max(0,T(cell)-phi(speed)) <= the cost left, and h(s) <= c(s,s')+h(s'). A strafe edge counts as
// taken from any cell within reach of its takeoff (the search also asks that the cell or a neighbour own it:
// dropping that only adds options). Bodies, the corridor, standing, avoided and off edges and a required edge only
// remove options or add cost, so the table ignores them; the clearance penalty is in it (entering a cell costs its
// penalty, GuideSettle) and a slot is keyed on bot_movePlanClearance. Two tables: wish g_speed for a state
// without Speed (its successors never gain it), and wish g_speed*1.7 with phi at that wish for a state with Speed
// (valid for a link at either wish). A strafe edge's reduced cost can be negative, so the table is built by a
// label-correcting search (a cell may be improved again), capped; past the cap the goal keeps the straight line.
// Learned edges only lower times: a new or cheaper edge is logged and its arcs added, and each table folds the log in
// at most every bot_movePlanGuideFoldMs; a dearer or switched-off edge leaves a table a lower bound. A load or a
// reset rebuilds the arcs. Goals listed in the map's .guide file are built at the load and never evicted.
// Costs are scaled by GUIDE_SAFETY (phi the other way) so float rounding cannot overestimate.
// ---------------------------------------------------------------------------
#define GUIDE_SLOTS 192
#define GUIDE_POOL (1<<22)              // floats for every slot's two tables, 2*graph.count a slot
#define GUIDE_ARCS (1<<20)
#define GUIDE_LOG 8192                  // edges added or made cheaper, in order; a slot folds those it has not seen
#define GUIDE_SAFETY .999f
static struct {
    int from[GUIDE_ARCS],next[GUIDE_ARCS];float cost[2][GUIDE_ARCS];  // reverse arcs into a cell, both tables' costs
    int head[GRAPH_MAX_CELLS];
    int count,overflow,built,cells,links,edgesKnown,merged;
    unsigned int topo;
} guideArcs;
static struct { int edge[GUIDE_LOG];unsigned int seq,arcsAt; } guideLog;
static struct { int goal,valid[2],clearance,pinned,foldAt;unsigned int used,seen; } guideSlot[GUIDE_SLOTS];
static float guidePool[GUIDE_POOL];
static int guideSlots;                  // slots the pool holds for this graph
static unsigned int guideUse;
static struct { int cell[GRAPH_MAX_CELLS],pos[GRAPH_MAX_CELLS],count;float key[GRAPH_MAX_CELLS]; } guideHeap;
static struct { int stamp[GRAPH_MAX_CELLS],arc[GRAPH_MAX_CELLS]; } guideMerge;
static const float *guideActive[2];     // the running search's tables; NULL: the straight line
static int guideActiveFresh;            // they have folded every logged edge, so an unreached cell has no route
static struct { int rebuilds,builds,folds,capped,preloaded,spread,waits; } guideStats;
// bot_movePlanGuideSpread (default 0): cells a frame a table settles while it builds; 0 builds a table whole when it is
// asked for. Spread, one table builds at a time on the shared heap (GuideSpreadRun, each frame from
// Graph_PathTableStep), a search asked while its table builds runs unguided, and folds wait for the build. Preloaded
// goals are built whole at the load.
static struct { int slot,t,pops,frameTime,frameSpent; } guidePend={-1};

static void GuideEdgeChanged(int edge,int moved) {
    (void)moved;   // a cheaper edge or a new takeoff: either way its arcs are added again with the edge's values now
    guideLog.edge[guideLog.seq%GUIDE_LOG]=edge;guideLog.seq++;
}

static float GuideWish(int t) {return graph.speed*(t?GRAPH_SPEED_FACTOR:1.0f);}
static float GuidePhi(float speed,float wish) {return speed>wish?(speed-wish)/(GRAPH_FRICTION*wish)*1000:0;}
static float *GuideTable(int slot,int t) {return guidePool+((size_t)slot*2+t)*graph.count;}

// An arc's reduced cost in table t: a link (kind -1) or diagonal (-2) at the wish speed, an edge at its cost (a
// strafe edge with the potential's change). Read from the edge when the arc is made; an edge made cheaper later is
// logged and gets a new arc.
static float GuideArcCost(int kind,int t) {
    float w=GuideWish(t);const graphEdge_t *e;
    if(kind==-1)return (float)GRAPH_GRID/w*1000*GUIDE_SAFETY;
    if(kind==-2)return GRAPH_GRID*1.41421f/w*1000*GUIDE_SAFETY;
    e=&graph.edge[kind];
    if(e->type!=GRAPH_EDGE_STRAFE)return e->costMs*GUIDE_SAFETY;
    return (e->costMs+GuidePhi(e->entrySpeed-GRAPH_STATE_BAND,w))*GUIDE_SAFETY-GuidePhi(e->exitSpeed,w)/GUIDE_SAFETY;
}

static void GuideCompactArcs(void);
static int guideArcsPacked;   // count holds only live arcs (GuideCompactArcs packed them)
// The live arcs, as the logs have always printed them (count less merged while merged arcs stayed in the array).
static int GuideLiveArcs(void) {return guideArcsPacked?guideArcs.count:guideArcs.count-guideArcs.merged;}
static void GuideArc(int to,int from,int kind) {
    int i;
    // Full: merging and compacting gives the repeats back (Kejim's load holds 852,000 arcs of which
    // 549,000 merge away); full after that, guidance stops until GuideSync rebuilds (guide_overflow).
    if(guideArcs.count>=GUIDE_ARCS)GuideCompactArcs();
    if(guideArcs.count>=GUIDE_ARCS) {
        if(!guideArcs.overflow)trap->Print("[GRAPH] guide_overflow arcs=%d cells=%d edges=%d\n",guideArcs.count,graph.count,graph.edgeCount);
        guideArcs.overflow=1;return;
    }
    i=guideArcs.count++;
    guideArcs.from[i]=from;guideArcs.cost[0][i]=GuideArcCost(kind,0);guideArcs.cost[1][i]=GuideArcCost(kind,1);
    guideArcs.next[i]=guideArcs.head[to];guideArcs.head[to]=i;
}

// An edge's arcs: from its own cell, or for a strafe edge from every cell within reach of its takeoff.
static void GuideEdgeArcs(int index) {
    const graphEdge_t *e;int cx,cy,ix,iy,i;
    if(index<0 || index>=graph.edgeCount)return;
    e=&graph.edge[index];
    if(e->to<0 || e->to>=graph.count)return;
    if(e->type!=GRAPH_EDGE_STRAFE) {if(e->from>=0 && e->from<graph.count)GuideArc(e->to,e->from,index);return;}
    cx=(int)floorf(e->takeoff[0]/GRAPH_GRID);cy=(int)floorf(e->takeoff[1]/GRAPH_GRID);
    for(ix=cx-2;ix<=cx+2;ix++)for(iy=cy-2;iy<=cy+2;iy++)
        for(i=graph.head[Bucket(ix,iy)];i>=0;i=graph.chain[i])
            if(graph.cell[i].ix==ix && graph.cell[i].iy==iy && CellReach(i,e->takeoff,GRAPH_STATE_REACH,2*STEPSIZE))
                GuideArc(e->to,i,index);
}

// One arc per (cell, predecessor) pair with each table's cheaper cost: the learned strafe edges between nearby
// cells repeat heavily (Kejim's 95,000 edges make 855,000 arcs), and the settle reads every arc.
static void GuideMergeArcs(void) {
    int to,a,prev;
    for(to=0;to<graph.count;to++) {
        for(a=guideArcs.head[to],prev=-1;a>=0;) {
            int f=guideArcs.from[a],next=guideArcs.next[a];
            if(guideMerge.stamp[f]==to+1) {
                int keep=guideMerge.arc[f];
                if(guideArcs.cost[0][a]<guideArcs.cost[0][keep])guideArcs.cost[0][keep]=guideArcs.cost[0][a];
                if(guideArcs.cost[1][a]<guideArcs.cost[1][keep])guideArcs.cost[1][keep]=guideArcs.cost[1][a];
                if(prev<0)guideArcs.head[to]=next;else guideArcs.next[prev]=next;
                guideArcs.merged++;
            } else {guideMerge.stamp[f]=to+1;guideMerge.arc[f]=a;prev=a;}
            a=next;
        }
    }
}

// Merge, then pack the live arcs to the front: merging only unlinked the repeats, so
// count kept them, every learned edge appended 7.9 more, and a full array stopped guidance for the rest of the map
// with nothing printed. Each cell's list keeps its order, so every settle relaxes the same arcs in the same order;
// count is the live arcs and merged the total merged away since the rebuild (the logs print count, as they printed
// count less merged before).
// The copy is a temporary allocation (16 bytes an arc): a rebuild and a full array are the only callers. Without the
// memory the arcs stay merged and unpacked, as before.
static void GuideCompactArcs(void) {
    int to,a,n=0,*from,*next;float *cost0,*cost1;
    memset(guideMerge.stamp,0,sizeof(int)*graph.count);
    GuideMergeArcs();
    if(!guideArcs.count)return;
    from=(int *)malloc(sizeof(int)*guideArcs.count);next=(int *)malloc(sizeof(int)*guideArcs.count);
    cost0=(float *)malloc(sizeof(float)*guideArcs.count);cost1=(float *)malloc(sizeof(float)*guideArcs.count);
    if(from && next && cost0 && cost1) {
        for(to=0;to<graph.count;to++) {
            int last=-1;
            for(a=guideArcs.head[to];a>=0;a=guideArcs.next[a]) {
                from[n]=guideArcs.from[a];cost0[n]=guideArcs.cost[0][a];cost1[n]=guideArcs.cost[1][a];next[n]=-1;
                if(last<0)guideArcs.head[to]=n;else next[last]=n;
                last=n++;
            }
        }
        memcpy(guideArcs.from,from,sizeof(int)*n);memcpy(guideArcs.next,next,sizeof(int)*n);
        memcpy(guideArcs.cost[0],cost0,sizeof(float)*n);memcpy(guideArcs.cost[1],cost1,sizeof(float)*n);
        guideArcs.count=n;guideArcsPacked=1;
    } else guideArcsPacked=0;
    free(from);free(next);free(cost0);free(cost1);
}

static void GuideHeapUp(int i) {
    int cell=guideHeap.cell[i];float key=guideHeap.key[i];
    while(i>0) {
        int p=(i-1)>>1;
        if(guideHeap.key[p]<=key)break;
        guideHeap.cell[i]=guideHeap.cell[p];guideHeap.key[i]=guideHeap.key[p];guideHeap.pos[guideHeap.cell[i]]=i+1;i=p;
    }
    guideHeap.cell[i]=cell;guideHeap.key[i]=key;guideHeap.pos[cell]=i+1;
}
static void GuideHeapSet(int cell,float key) {
    int i=guideHeap.pos[cell]-1;
    if(i<0) {i=guideHeap.count++;guideHeap.cell[i]=cell;}
    guideHeap.key[i]=key;GuideHeapUp(i);   // keys only fall
}
static int GuideHeapPop(void) {
    int top=guideHeap.cell[0],i=0,child,cell;float key;
    guideHeap.pos[top]=0;
    if(--guideHeap.count<=0)return top;
    cell=guideHeap.cell[guideHeap.count];key=guideHeap.key[guideHeap.count];
    for(;;) {
        child=2*i+1;
        if(child>=guideHeap.count)break;
        if(child+1<guideHeap.count && guideHeap.key[child+1]<guideHeap.key[child])child++;
        if(guideHeap.key[child]>=key)break;
        guideHeap.cell[i]=guideHeap.cell[child];guideHeap.key[i]=guideHeap.key[child];guideHeap.pos[guideHeap.cell[i]]=i+1;i=child;
    }
    guideHeap.cell[i]=cell;guideHeap.key[i]=key;guideHeap.pos[cell]=i+1;
    return top;
}

// Settles a table from the cells already in the heap; 0 when it ran past its cap (the table is then unusable).
static int GuideSettle(float *time,int t) {
    int pops=0,cap=64*graph.count+1024;const float *cost=guideArcs.cost[t];
    while(guideHeap.count>0) {
        int cell=GuideHeapPop(),a;float base;
        bot2_meterGuidePops++;
        if(++pops>cap) {
            while(guideHeap.count>0)GuideHeapPop();
            guideStats.capped++;
            return 0;
        }
        base=time[cell]+ClearancePenalty(cell)*GUIDE_SAFETY;   // entering a cell costs its clearance penalty
        for(a=guideArcs.head[cell];a>=0;a=guideArcs.next[a]) {
            int f=guideArcs.from[a];float d=base+cost[a];
            if(d<time[f]) {time[f]=d;GuideHeapSet(f,d);}
        }
    }
    return 1;
}

// Part of the pending build: at most budget settles; 1 done, 0 past the cap (the table is then unusable), 2 more to do.
static int GuideSettleStep(float *time,int t,int budget) {
    int cap=64*graph.count+1024;const float *cost=guideArcs.cost[t];
    while(guideHeap.count>0) {
        int cell,a;float base;
        if(budget--<=0)return 2;
        cell=GuideHeapPop();
        bot2_meterGuidePops++;
        if(++guidePend.pops>cap) {
            while(guideHeap.count>0)GuideHeapPop();
            guideStats.capped++;
            return 0;
        }
        base=time[cell]+ClearancePenalty(cell)*GUIDE_SAFETY;
        for(a=guideArcs.head[cell];a>=0;a=guideArcs.next[a]) {
            int f=guideArcs.from[a];float d=base+cost[a];
            if(d<time[f]) {time[f]=d;GuideHeapSet(f,d);}
        }
    }
    return 1;
}

static int GuideSpread(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePlanGuideSpread",value,sizeof(value));
    return value[0]?atoi(value):0;
}

// Drops the pending build (its table stays invalid).
static void GuidePendCancel(void) {
    if(guidePend.slot<0)return;
    while(guideHeap.count>0)GuideHeapPop();
    guideSlot[guidePend.slot].valid[guidePend.t]=0;
    guidePend.slot=-1;
}

// Spends what this frame's budget has left on the pending build; a budget of 0 (switched off part-way) finishes it.
static void GuideSpreadRun(void) {
    int budget=GuideSpread(),r,slot=guidePend.slot,t=guidePend.t;unsigned int before=bot2_meterGuidePops;
    if(slot<0)return;
    if(guidePend.frameTime!=Now()) {guidePend.frameTime=Now();guidePend.frameSpent=0;}
    if(budget<=0)budget=1<<30;
    budget-=guidePend.frameSpent;
    if(budget<=0)return;
    r=GuideSettleStep(GuideTable(slot,t),t,budget);
    guidePend.frameSpent+=(int)(bot2_meterGuidePops-before);
    if(r==2)return;
    guideSlot[slot].valid[t]=r;guidePend.slot=-1;
    guideStats.builds++;guideStats.spread++;
}

// Starts a spread build of one table and runs this frame's part: 1 when that finished it.
static int GuideStart(int slot,int t) {
    int i,goal=guideSlot[slot].goal;float *time=GuideTable(slot,t);
    for(i=0;i<graph.count;i++)time[i]=GUIDE_UNREACHED;
    time[goal]=0;GuideHeapSet(goal,0);
    guideSlot[slot].valid[t]=0;
    guidePend.slot=slot;guidePend.t=t;guidePend.pops=0;
    GuideSpreadRun();
    return guidePend.slot<0 && guideSlot[slot].valid[t];
}

// One table of a slot: t 0 without Speed, 1 with it.
static int GuideBuild(int slot,int t) {
    int i,goal=guideSlot[slot].goal;float *time=GuideTable(slot,t);
    for(i=0;i<graph.count;i++)time[i]=GUIDE_UNREACHED;
    time[goal]=0;GuideHeapSet(goal,0);
    guideSlot[slot].valid[t]=GuideSettle(time,t);
    guideStats.builds++;
    return guideSlot[slot].valid[t];
}

// The arcs current with the graph: rebuilt on a load or reset (every table then built again when next asked for),
// else extended by the edges logged since (new ones included), which each slot folds in when it next folds.
static void GuideSync(void) {
    int i,d,s;
    for(i=guideArcs.built?guideArcs.edgesKnown:graph.edgeCount;i<graph.edgeCount;i++)GuideEdgeChanged(i,0);
    if(!guideArcs.built || guideArcs.topo!=guideTopo || guideArcs.cells!=graph.count || guideArcs.links!=graph.links ||
       graph.edgeCount<guideArcs.edgesKnown || guideLog.seq-guideLog.arcsAt>GUIDE_LOG || guideArcs.overflow==1) {
        int sameCells,again;
        GuidePendCancel();   // its heap and table are for the old arcs
        sameCells=guideArcs.built && guideArcs.cells==graph.count;again=guideArcs.overflow==1 && guideArcs.built &&
            guideArcs.topo==guideTopo && guideArcs.cells==graph.count && guideArcs.links==graph.links;
        guideArcs.count=0;guideArcs.overflow=0;guideArcs.merged=0;guideArcsPacked=1;
        for(i=0;i<graph.count;i++)guideArcs.head[i]=-1;
        for(i=0;i<graph.count;i++) {
            const graphCell_t *c=&graph.cell[i];
            for(d=0;d<4;d++)if(c->link[d]>=0)GuideArc(c->link[d],i,-1);
            for(d=0;d<4;d++) {
                int a=c->link[d],b=c->link[(d+1)&3];
                if(a>=0 && b>=0 && graph.cell[a].link[(d+1)&3]>=0 && graph.cell[a].link[(d+1)&3]==graph.cell[b].link[d])
                    GuideArc(graph.cell[a].link[(d+1)&3],i,-2);
            }
        }
        for(i=0;i<graph.edgeCount;i++)GuideEdgeArcs(i);
        GuideCompactArcs();
        if(guideArcs.overflow && again)guideArcs.overflow=2;   // a rebuild for an overflow overflowed: no third try
        guideArcs.built=1;guideArcs.topo=guideTopo;guideArcs.cells=graph.count;guideArcs.links=graph.links;
        guideArcs.edgesKnown=graph.edgeCount;guideLog.arcsAt=guideLog.seq;
        guideSlots=min(GUIDE_SLOTS,GUIDE_POOL/max(1,2*graph.count));
        for(s=0;s<GUIDE_SLOTS;s++) {
            guideSlot[s].valid[0]=guideSlot[s].valid[1]=0;
            if(!sameCells || s>=guideSlots)guideSlot[s].pinned=0;   // cell indices kept: a pinned goal is built again
        }
        guideStats.rebuilds++;
        return;
    }
    for(;guideLog.arcsAt!=guideLog.seq;guideLog.arcsAt++)GuideEdgeArcs(guideLog.edge[guideLog.arcsAt%GUIDE_LOG]);
    guideArcs.edgesKnown=graph.edgeCount;
}

// bot_movePlanGuideFoldCap (default 0 = no cap): cells a frame that timed folds may settle. A fold
// asked for when this frame's folds have already settled that many is put off (the slot keeps its tables as they
// are, as between two folds) and runs when the goal is next asked for in a later frame. A fold runs whole once
// started, so a frame settles at most the cap plus one fold. A fold made to build the Speed table is not put off.
static struct { int frameTime,frameSpent; } guideFoldCap;
static int GuideFoldCap(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePlanGuideFoldCap",value,sizeof(value));
    return value[0]?atoi(value):0;
}

// Folds a slot's tables up to the log: settling from each logged edge's landing relaxes its new arcs.
static void GuideFold(int slot) {
    int t;unsigned int k,before=bot2_meterGuidePops;
    if(guideSlot[slot].seen==guideLog.seq)return;
    if(guideLog.seq-guideSlot[slot].seen>GUIDE_LOG) {guideSlot[slot].valid[0]=guideSlot[slot].valid[1]=0;return;}
    for(t=0;t<2;t++)if(guideSlot[slot].valid[t]) {
        float *time=GuideTable(slot,t);
        for(k=guideSlot[slot].seen;k!=guideLog.seq;k++) {
            int e=guideLog.edge[k%GUIDE_LOG],to;
            if(e<0 || e>=graph.edgeCount)continue;
            to=graph.edge[e].to;
            if(to>=0 && to<graph.count && time[to]<GUIDE_UNREACHED)GuideHeapSet(to,time[to]);
        }
        if(!GuideSettle(time,t))guideSlot[slot].valid[t]=0;
    }
    guideSlot[slot].seen=guideLog.seq;guideSlot[slot].foldAt=Now();
    guideStats.folds++;
    bot2_meterGuideFoldPops+=bot2_meterGuidePops-before;
    if(guideFoldCap.frameTime!=Now()) {guideFoldCap.frameTime=Now();guideFoldCap.frameSpent=0;}
    guideFoldCap.frameSpent+=(int)(bot2_meterGuidePops-before);
}

// bot_movePlanGuideFoldMs (default 1000): how often a slot folds the edges learned since it last did; between
// folds it does not know them (a route over a new edge looks no faster than before) and prunes nothing as unreached.
static int GuideFoldMs(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePlanGuideFoldMs",value,sizeof(value));
    return value[0]?atoi(value):1000;
}

// The slot holding a goal's tables (the Speed table too when speed is set), built when absent; -1 when guidance
// cannot serve it. The clearance penalty is in the tables, so a change of bot_movePlanClearance drops them.
static int GuideFor(int goal,int speed) {
    int s,slot=-1,spread;
    if(goal<0 || goal>=graph.count)return -1;
    ClearanceRead();
    GuideSync();
    if(guideArcs.overflow || guideSlots<1)return -1;   // GuideSync rebuilt once for an overflow and it still overflows
    if(guidePend.slot>=0 && guideSlot[guidePend.slot].clearance!=clearanceCells)GuidePendCancel();
    spread=GuideSpread();
    for(s=0;s<guideSlots;s++)if(guideSlot[s].goal==goal && guideSlot[s].clearance==clearanceCells &&
                                (guideSlot[s].valid[0] || guideSlot[s].pinned || s==guidePend.slot)) {slot=s;break;}
    if(slot>=0 && slot==guidePend.slot && (guidePend.t==0 || speed)) {guideStats.waits++;return -1;}   // still building
    if(slot>=0 && guideSlot[slot].valid[0] && guidePend.slot<0) {   // a fold uses the heap a spread build holds
        if(guideSlot[slot].seen!=guideLog.seq) {
            if(speed && !guideSlot[slot].valid[1])GuideFold(slot);
            else if(Now()-guideSlot[slot].foldAt>=GuideFoldMs()) {
                int cap=GuideFoldCap();
                if(cap>0 && guideFoldCap.frameTime==Now() && guideFoldCap.frameSpent>=cap)bot2_meterGuideFoldDeferred++;
                else GuideFold(slot);
            }
        }
    }
    if(slot<0) {
        if(guidePend.slot>=0) {guideStats.waits++;return -1;}   // one build at a time: asked again, it is built then
        for(s=0;s<guideSlots;s++) {
            if(guideSlot[s].pinned)continue;
            if(slot<0 || !guideSlot[s].valid[0] || guideSlot[s].used<guideSlot[slot].used)slot=s;
            if(!guideSlot[s].valid[0])break;
        }
        if(slot<0)return -1;
        guideSlot[slot].goal=goal;guideSlot[slot].clearance=clearanceCells;guideSlot[slot].valid[0]=guideSlot[slot].valid[1]=0;
    }
    if(!guideSlot[slot].valid[0]) {
        if(guidePend.slot>=0) {guideStats.waits++;return -1;}
        guideSlot[slot].valid[1]=0;guideSlot[slot].seen=guideLog.seq;guideSlot[slot].foldAt=Now();
        if(spread>0?!GuideStart(slot,0):!GuideBuild(slot,0))return -1;
    }
    guideSlot[slot].used=++guideUse;
    if(speed && !guideSlot[slot].valid[1]) {
        if(guidePend.slot>=0) {guideStats.waits++;return -1;}
        if(spread>0?!GuideStart(slot,1):!GuideBuild(slot,1))return -1;
    }
    return slot;
}

// The goals worth tables from the map's start (bot_movePlanGuide on at the load): bot2-graph/<map>_<checksum>.guide,
// one "g x y z" a line (tools/movement_guide_goals.py writes it from earlier worlds' [PLANGUIDE] lines), each built
// at the load for both tables and kept for the whole map.
static void GuidePreload(void) {
    char path[MAX_QPATH],*bytes,*line,*next;fileHandle_t file=0;int size,goals=0,built=0;double began;
    {char value[16];trap->Cvar_VariableStringBuffer("bot_movePlanGuide",value,sizeof(value));if((value[0]?atoi(value):1)<=0)return;}
    CacheBase(path,sizeof(path));
    Q_strcat(path,sizeof(path),".guide");   // goal positions: one list for every physics binding
    size=trap->FS_Open(path,&file,FS_READ);
    if(!file || size<=0) {if(file)trap->FS_Close(file);return;}
    bytes=(char *)malloc(size+1);
    if(!bytes) {trap->FS_Close(file);return;}
    trap->FS_Read(bytes,size,file);trap->FS_Close(file);bytes[size]=0;
    began=Bot2_ClockUs();
    ClearanceRead();GuideSync();GuidePendCancel();
    for(line=bytes;line && *line && !guideArcs.overflow;line=next) {
        vec3_t at;int cell,s,slot=-1;
        next=strchr(line,'\n');if(next)*next++=0;
        if(sscanf(line,"g %f %f %f",&at[0],&at[1],&at[2])!=3)continue;
        goals++;
        cell=Graph_CellAt(at,STEPSIZE+.25f);
        if(cell<0)cell=Graph_Nearest(at,48,2*STEPSIZE);
        if(cell<0)continue;
        for(s=0;s<guideSlots;s++)if(guideSlot[s].pinned && guideSlot[s].goal==cell)break;
        if(s<guideSlots)continue;
        for(s=0;s<guideSlots;s++)if(!guideSlot[s].pinned) {slot=s;break;}
        if(slot<0)break;
        guideSlot[slot].goal=cell;guideSlot[slot].clearance=clearanceCells;guideSlot[slot].pinned=1;
        guideSlot[slot].seen=guideLog.seq;guideSlot[slot].foldAt=Now();guideSlot[slot].used=++guideUse;
        if(GuideBuild(slot,0) && GuideBuild(slot,1))built++;
    }
    free(bytes);
    guideStats.preloaded=built;
    trap->Print("[GRAPH] guide_preload file=%s goals=%d built=%d slots=%d arcs=%d merged=%d ms=%.1f\n",path,goals,built,
        guideSlots,GuideLiveArcs(),guideArcs.merged,(Bot2_ClockUs()-began)/1000);
}

// Whether a search is worth guiding. A preloaded goal always is: its tables exist, and the straight line is worst
// toward a goal close above or below (Kejim: about 400 units by the line, 18 s by the route, 20,000 expansions).
// Another goal once this map's searches have asked for it bot_movePlanGuideAsks times (default 3): the goals a match
// makes its own (a camp, a post) recur dozens of times and differ between matches, and a table costs a few ms; a
// moving goal's cells seldom recur. With bot_movePlanGuideMinMs (default 0) a goal must also be that far by the
// straight line.
static struct { unsigned char count[GRAPH_MAX_CELLS];int cells; } guideAsked;
static int GuideWanted(int from,int to) {
    char value[16];int k,asks,minMs;
    if(from<0 || to<0 || from>=graph.count || to>=graph.count)return 0;
    for(k=0;k<guideSlots;k++)if(guideSlot[k].pinned && guideSlot[k].goal==to)return 1;
    if(guideAsked.cells!=graph.count) {memset(guideAsked.count,0,sizeof(guideAsked.count));guideAsked.cells=graph.count;}
    trap->Cvar_VariableStringBuffer("bot_movePlanGuideMinMs",value,sizeof(value));
    minMs=value[0]?atoi(value):0;
    if(minMs>0 && HeuristicOf(from,to)<minMs)return 0;
    trap->Cvar_VariableStringBuffer("bot_movePlanGuideAsks",value,sizeof(value));
    asks=value[0]?atoi(value):3;
    if(guideAsked.count[to]<255)guideAsked.count[to]++;
    return guideAsked.count[to]>=asks;
}

// Whether a goal still holds a valid table (bot_movePlanGuideLong: a goal whose search once ran long keeps being
// guided from the start, whatever its ask count, so the discarded expansions are paid once a goal).
static int GuideHeld(int to) {
    int s;
    if(to<0 || to>=graph.count || !guideArcs.built)return 0;
    ClearanceRead();
    GuideSync();
    for(s=0;s<guideSlots;s++)if(guideSlot[s].goal==to && guideSlot[s].clearance==clearanceCells && guideSlot[s].valid[0])return 1;
    return 0;
}

// bot_movePlanGuideMax (default 1; 0 the table alone): 1 takes the larger of the table's bound and
// the straight line. Not a lower bound
// (the straight line is not one on fast routes either), but it keeps the straight line's pull where hops beat it.
static int guideMax;
static float StateHeuristic(int cell,const graphState_t *s,int goal) {
    int t;float time,h;
    if(!guideActive[0])return Heuristic(cell,goal);
    t=s->speedMs>0;time=guideActive[t][cell];
    if(time>=GUIDE_UNREACHED)return guideActiveFresh?GUIDE_UNREACHED:Heuristic(cell,goal);
    h=time-GuidePhi(s->speed,GuideWish(t))/GUIDE_SAFETY;
    if(guideMax) {float line=Heuristic(cell,goal);if(line>h)h=line;}
    return h>0?h:0;
}

// The state search in three parts, so one can run over several frames in a
// table of its own (Graph_PlanSplit*): StateBegin seeds the root, StateRun
// expands up to a budget (0 none) and says when the search is over,
// StateResult reads the route. Run in one call they are the search exactly.
static int StateBegin(int from,const graphState_t *start,int to,const int *avoid,int avoidCount,int require) {
    int created,root;graphState_t s;
    stateSearch.done=1;stateSearch.goalEntry=-1;stateSearch.expanded=0;stateSearch.from=from;stateSearch.to=to;
    stateJoin.best=-1;stateJoin.joined=-1;
    if(++stateSearch.stamp==0) {    // wrapped: the one call in 2^32 that pays for the memset
        {int k;for(k=0;k<GRAPH_STATE_MAX*2;k++)stateSearch.slot[k].stamp=0;}stateSearch.stamp=1;
    }
    stateSearch.count=0;stateSearch.heapCount=0;stateSearch.overflow=0;
    stateSearch.require=require>=0 && require<graph.edgeCount?require:-1;
    if(require>=0 && stateSearch.require<0)return 0;   // an edge this graph does not have: no route satisfies it
    stateSearch.avoidCount=avoid?min(avoidCount,GRAPH_STATE_AVOID):0;
    if(stateSearch.avoidCount>0)memcpy(stateSearch.avoid,avoid,sizeof(int)*stateSearch.avoidCount);
    s=*start;
    if(s.speed<0)s.speed=0;
    if(s.dir[0]*s.dir[0]+s.dir[1]*s.dir[1]<.5f) {s.dir[0]=1;s.dir[1]=0;}
    StateAvoidMark(stateSearch.avoid,stateSearch.avoidCount);heuristicMemo.goal=-1;
    root=StateEntry(StateKey(from,&s,0),from,&s,0,&created);
    if(root<0)return 0;
    stateSearch.entry[root].g=0;
    StateHeapPush(root,StateHeuristic(from,&s,to));
    stateSearch.done=0;
    return 1;
}

static int StateRun(int budget) {
    int d,e,nearCount,nearFirst,ran=0,to=stateSearch.to;const int *avoid=stateSearch.avoid;int avoidCount=stateSearch.avoidCount;
    int joining=stateJoin.active && stateSearch.require<0;
    if(stateSearch.done)return 1;
    while(stateSearch.heapCount>0 && !stateSearch.overflow) {
        int at,cell;graphStateEntry_t *x;
        if(budget>0 && ran>=budget)return 0;
        if(joining && stateJoin.best>=0 && stateSearch.heap[0].key+stateJoin.slack>=stateJoin.bestCost)break;
        if(planBound.armed && stateSearch.heap[0].key>planBound.arm)return 0;   // PlanStateStaged takes the cell route
        if(planBound.live) {    // only inside PlanStateStaged, once its cell route is known
            float key=stateSearch.heap[0].key;
            while(planBound.step<PLAN_BOUND_STEPS && key>planBound.at[planBound.step])
                planBound.stepExpanded[planBound.step++]=stateSearch.expanded;
            if(planBound.limit>0 && key>planBound.limit) {planBound.stopped=1;break;}
        }
        at=StateHeapPop();x=&stateSearch.entry[at];cell=x->cell;
        if(x->closed)continue;
        x->closed=1;stateSearch.expanded++;ran++;bot2_meterExpanded++;if(bot2_meterInReplan==1)bot2_meterExpandedReplan++;
        if(cell==to && (stateSearch.require<0 || x->taken)) {stateSearch.goalEntry=at;break;}
        if(joining && stateJoin.stamp[cell]==stateJoin.now) {
            if(stateJoin.best<0 || x->g+stateJoin.rest[cell]<stateJoin.bestCost) {
                stateJoin.best=at;stateJoin.bestCost=x->g+stateJoin.rest[cell];
            }
        }
        for(d=0;d<4;d++)if(graph.cell[cell].link[d]>=0) {
            float dir[2]={(float)graphDir[d][0],(float)graphDir[d][1]};
            StateLink(at,graph.cell[cell].link[d],(float)GRAPH_GRID,dir,to);
        }   // StateWalk's memo makes the second to fourth links of a kind a lookup
        for(d=0;d<4;d++) {
            int a=graph.cell[cell].link[d],b=graph.cell[cell].link[(d+1)&3];
            if(a>=0 && b>=0 && graph.cell[a].link[(d+1)&3]>=0 && graph.cell[a].link[(d+1)&3]==graph.cell[b].link[d]) {
                float dir[2]={(graphDir[d][0]+graphDir[(d+1)&3][0])*.7071f,(graphDir[d][1]+graphDir[(d+1)&3][1])*.7071f};
                StateLink(at,graph.cell[a].link[(d+1)&3],GRAPH_GRID*1.41421f,dir,to);
            }
        }
        // Edges of the cell itself: the retained kinds leave the walk's
        // state; strafe edges their own. Strafe edges of the neighbouring
        // cells within the takeoff's reach are admitted too, since a landing
        // rarely repeats a cell exactly.
        for(e=graph.cell[cell].firstEdge;e>=0;e=graph.edge[e].nextFrom) {
            const graphEdge_t *edge=&graph.edge[e];graphState_t next;
            if(Graph_EdgeOff(edge) || edge->type==GRAPH_EDGE_STRAFE || StateAvoided(e,avoid,avoidCount))continue;
            next.speed=StateWish(&x->state);next.dir[0]=edge->dir[0];next.dir[1]=edge->dir[1];next.tap=0;
            next.speedMs=max(0,x->state.speedMs-edge->costMs);
            StateRelax(at,edge->to,&next,e,(float)edge->costMs,to);
        }
        nearCount=StrafeNear(cell,&nearFirst);
        if(nearCount>=0) {
            float cone=StateConeCos();
            for(e=0;e<nearCount;e++) {
                // The state tests of StateStrafeEdge on the arena's copies first; the edge is read only when they pass.
                const graphStateEntry_t *xs=&stateSearch.entry[at];const int k=nearFirst+e;
                if(strafeNear.entry[k].tap && !xs->state.tap)continue;
                if(fabsf(xs->state.speed-strafeNear.entry[k].speed)>GRAPH_STATE_BAND)continue;
                if(xs->state.dir[0]*strafeNear.entry[k].dir[0]+xs->state.dir[1]*strafeNear.entry[k].dir[1]<cone)continue;
                StateStrafeEdge(at,strafeNear.edge[k],cone,avoid,avoidCount,to);
            }
        } else {
            StateStrafeEdges(at,cell,cell,avoid,avoidCount,to);
            for(d=0;d<4;d++) {
                int a=graph.cell[cell].link[d],b;
                if(a<0)continue;
                StateStrafeEdges(at,cell,a,avoid,avoidCount,to);
                b=graph.cell[a].link[(d+1)&3];
                if(b>=0)StateStrafeEdges(at,cell,b,avoid,avoidCount,to);
            }
        }
    }
    if(joining && stateSearch.goalEntry<0 && stateJoin.best>=0) {
        stateSearch.goalEntry=stateJoin.best;stateJoin.joined=stateSearch.entry[stateJoin.best].cell;
    }
    stateSearch.done=1;
    return 1;
}

static int StateResult(int *cells,int *edges,float *speeds,int max,int *costMs,int *expanded,int *overflow) {
    int i,d,count=0,goalEntry=stateSearch.goalEntry;
    *expanded=stateSearch.expanded;*overflow=stateSearch.overflow;
    stateTimes.count=0;
    if(goalEntry<0)return 0;
    for(i=goalEntry;i>=0;i=stateSearch.entry[i].parent)count++;
    for(i=goalEntry,d=count-1;i>=0;i=stateSearch.entry[i].parent,d--)if(d<max) {   // the first max cells
        cells[d]=stateSearch.entry[i].cell;edges[d]=stateSearch.entry[i].parentEdge;
        if(speeds)speeds[d]=stateSearch.entry[i].state.speed;
        if(d<GRAPH_MAX_PATH)stateTimes.g[d]=stateSearch.entry[i].g;
    }
    if(costMs)*costMs=(int)stateSearch.entry[goalEntry].g;
    if(count>max) {PathTruncated("state",-1,stateSearch.entry[goalEntry].cell,count,max);count=max;}
    stateTimes.count=count<=GRAPH_MAX_PATH?count:0;
    return count;
}

static int PlanStateSearch(int from,const graphState_t *start,int to,const int *avoid,int avoidCount,int require,
                    int *cells,int *edges,float *speeds,int max,int *costMs,int *expanded,int *overflow) {
    *expanded=0;*overflow=0;
    ClearanceRead();
    if(from<0 || to<0 || from>=graph.count || to>=graph.count || max<1)return 0;
    if(!StateBegin(from,start,to,avoid,avoidCount,require))return 0;
    StateRun(0);
    return StateResult(cells,edges,speeds,max,costMs,expanded,overflow);
}

// The replan budget's cvars (records/2026-11-09-replan-budget.md), all default 0 = off; with both N at 0 every
// search is PlanStateSearch as before.
// bot_movePlanGuideLong N: an unguided search not finished after N expansions gets its goal's tables and starts over.
// bot_movePlanBound N: a search not finished after N expansions is bounded by bot_movePlanBoundPct percent of the
// cell route's cost (taken once its lowest key passes that percent of the root's estimate; 0 = 300 for the check);
// bot_movePlanBoundCheck 1 only prints what each bound would have done.
static int PlanCvar(const char *name) {
    char value[16];
    trap->Cvar_VariableStringBuffer(name,value,sizeof(value));
    return value[0]?max(0,atoi(value)):0;
}

// The state search in stages: PlanStateSearch exactly while it is short. guideSpeed: the Speed flag for the goal's
// tables when the search starts unguided under guidance (-1: it is guided already, or guidance is off).
static int PlanStateStaged(int from,const graphState_t *start,int to,const int *avoid,int avoidCount,int require,
                    int *cells,int *edges,float *speeds,int max,int *costMs,int *expanded,int *overflow,
                    int longN,int guideSpeed,int boundN) {
    int count,discarded=0,slot=-1,builds=guideStats.builds,taken=0,takenAt=0,avoided=0,k;unsigned int pops=bot2_meterGuidePops;
    int pct=boundN>0?PlanCvar("bot_movePlanBoundPct"):0,check=boundN>0?PlanCvar("bot_movePlanBoundCheck"):0;
    float hRoot=-1;
    *expanded=0;*overflow=0;
    ClearanceRead();planBound.valid=0;planBound.live=0;planBound.armed=0;
    if(from<0 || to<0 || from>=graph.count || to>=graph.count || max<1)return 0;
    if(!StateBegin(from,start,to,avoid,avoidCount,require))return 0;
    if(require>=0) {longN=0;boundN=0;}   // a directed search (fixtures) stays whole
    if(longN>0 && guideSpeed>=0 && !StateRun(longN)) {
        slot=GuideFor(to,guideSpeed);
        if(slot>=0) {
            discarded=stateSearch.expanded;
            guideActive[0]=GuideTable(slot,0);guideActive[1]=GuideTable(slot,1);guideActiveFresh=guideSlot[slot].seen==guideLog.seq;
            if(!StateBegin(from,start,to,avoid,avoidCount,require)) {*expanded=discarded;return 0;}
        }
    }
    if(boundN>0 && (pct>0 || check) && !StateRun(max(1,boundN-stateSearch.expanded))) {
        graphState_t s=*start;
        if(s.speed<0)s.speed=0;
        if(pct<=0)pct=300;
        hRoot=StateHeuristic(from,&s,to);
        planBound.arm=hRoot<GUIDE_UNREACHED?hRoot*(pct*.01f):0;planBound.armed=1;
        if(!StateRun(0)) {
            // Paused past the root's estimate. The cell search runs in tables of its own; it restarts the heuristic
            // memo, whose values are pure.
            planBound.armed=0;
            takenAt=stateSearch.expanded;
            planBound.count=PathSearch(from,to,planBound.cells,planBound.edges,GRAPH_MAX_PATH,&planBound.costMs);
            // Kept whatever it found: "no cell route" is an answer too, and spares the caller the same search.
            planBound.from=from;planBound.to=to;planBound.valid=1;
            if(planBound.count>0) {
                taken=1;
                for(k=0;k<planBound.count;k++)if(planBound.edges[k]>=0 && StateAvoided(planBound.edges[k],avoid,avoidCount))avoided=1;
                planBound.step=0;planBound.stopped=0;
                for(k=0;k<PLAN_BOUND_STEPS;k++) {planBound.at[k]=planBound.costMs*(planBoundPct[k]*.01f);planBound.stepExpanded[k]=-1;}
                planBound.limit=check || avoided?0:planBound.costMs*(pct*.01f);
                planBound.live=1;
            }
        }
        planBound.armed=0;
    }
    StateRun(0);
    planBound.live=0;
    count=StateResult(cells,edges,speeds,max,costMs,expanded,overflow);
    *expanded+=discarded;
    if(count>0)planBound.valid=0;   // a state route: nobody falls back
    if(slot>=0)
        trap->Print("[PLANLONG] level=%d from=%d to=%d n=%d slot=%d builds=%d pops=%u discarded=%d expanded=%d count=%d cost=%d\n",
            level.time,from,to,longN,slot,guideStats.builds-builds,bot2_meterGuidePops-pops,discarded,*expanded,count,
            count?*costMs:-1);
    if(taken)
        trap->Print("[PLANBOUND] level=%d from=%d to=%d n=%d guided=%d check=%d pct=%d h_root=%.0f avoided=%d cell_cells=%d cell_cost=%d "
            "taken_at=%d stopped=%d at100=%d at125=%d at150=%d at200=%d at300=%d expanded=%d overflow=%d count=%d cost=%d\n",
            level.time,from,to,boundN,guideActive[0]!=NULL,check,pct,hRoot,avoided,planBound.count,planBound.costMs,takenAt,
            planBound.stopped,planBound.stepExpanded[0],planBound.stepExpanded[1],planBound.stepExpanded[2],
            planBound.stepExpanded[3],planBound.stepExpanded[4],*expanded,*overflow,count,count?*costMs:-1);
    return count;
}

int Graph_PlanBoundRoute(int from,int to,int *cells,int *edges,int max,int *costMs) {
    int count=planBound.count;
    if(!planBound.valid || planBound.from!=from || planBound.to!=to || count>max)return 0;
    planBound.valid=0;
    if(count<1)return -1;
    memcpy(cells,planBound.cells,sizeof(int)*count);memcpy(edges,planBound.edges,sizeof(int)*count);
    if(costMs)*costMs=planBound.costMs;
    return count;
}

// The two planners as callers see them: the searches above, charged to the
// cost fixture's frame when a capture is active (row 5 attribution). The
// clock is read only then; otherwise these are the searches exactly.
int Graph_Path(int from,int to,int *cells,int *edges,int max,int *costMs) {
    double began=Bot2_MoveCostClock();int count;
    OffExpire();count=PathSearch(from,to,cells,edges,max,costMs);
    Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_GRAPHPATH,began);
    Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_GRAPHPATH_CALLS);
    return count;
}

// ---------------------------------------------------------------------------
// Route tables for the cell search (bot_movePathTables, records/2026-10-06-frame-cost.md, "where the
// planner expansions go"). The defenders' route queries and the dropped-flag check ask Graph_Path for routes to a
// few fixed cells (a stand, a flag) from wherever someone is, each a whole cross-map search: 34-41% of Kejim's and
// 37-38% of Dantooine's planner expansions in frames of 10,000 or more. A table holds, for one goal, every cell's
// cheapest time to it under the cell search's own costs (walk links, diagonals, edges not off, the clearance penalty
// on the cell entered, only standing cells entered), by Dijkstra over the reversed arcs; a query walks down it.
//  - A goal gets a table once asked PATH_TABLE_ASKS times since the last load; the table is built over frames,
//    PATH_BUILD_BUDGET settled cells per 25 ms of frame (Graph_PathTableStep, scaled to the frame length), and the
//    search answers until it is ready.
//  - An edge made available or cheaper is relaxed into each table; one turned off, disabled or made dearer repairs
//    only the cells whose cheapest time ran through it (none when its arc was not tight). A load, a reset, the
//    clearance or a lift's state rebuilds.
// The cell search is A* with a heuristic that edges faster than GRAPH_HEURISTIC_SPEED can beat, so a table route is
// never dearer (but for float ties of a millisecond) and is often cheaper than its result; ties may pick other cells.
// Searches under bodies or a corridor use the search.
// ---------------------------------------------------------------------------
#define PATH_TABLES 32
#define PATH_TABLE_ASKS 3
#define PATH_BUILD_BUDGET 4000
#define PATH_UNREACHED 1e30f
typedef struct { float key;int cell; } pathHeapEntry_t;
typedef struct { pathHeapEntry_t *e;int count; } pathHeap_t;
static pathHeapEntry_t pathHeapWorkStore[GRAPH_MAX_CELLS*8+GRAPH_MAX_EDGES],pathHeapBuildStore[GRAPH_MAX_CELLS*8+GRAPH_MAX_EDGES];
static pathHeap_t pathWork={pathHeapWorkStore,0},pathBuild={pathHeapBuildStore,0};
static struct {
    int goal,used,ready;
    unsigned int topo,full,decreaseSeq;int clearance,cells,links,moverCells,moverStand;   // what it was built under
    float dist[GRAPH_MAX_CELLS];
} pathTable[PATH_TABLES];
static int pathTableUse,pathBuilding=-1,pathStepTime=-1;
// Why tables were built or repaired (diagnostic).
static struct { int queries,fresh,topo,full,clearance,cells,mover,ring,incremental,current,printed,tight,repaired,searched,building; } pathStats;
// Asks per goal cell since the last load or reset, for PATH_TABLE_ASKS.
static struct { unsigned int topo;unsigned char count[GRAPH_MAX_CELLS]; } pathAsks;
// The reversed walk and diagonal arcs (static between loads) and, per cell, the edges landing on it.
static struct {
    unsigned int topo;int cells,links,built;
    int start[GRAPH_MAX_CELLS+1];
    struct { int from;short ms; } arc[GRAPH_MAX_CELLS*8];
    int edgeHead[GRAPH_MAX_CELLS],edgeNext[GRAPH_MAX_EDGES],edges;
} pathRev;
static struct { unsigned int stamp[GRAPH_MAX_CELLS],now;int list[GRAPH_MAX_CELLS]; } pathMark;

static void PathHeapPush(pathHeap_t *h,int cell,float key) {
    int i=h->count++;
    while(i>0) {int up=(i-1)/2;if(h->e[up].key<=key)break;h->e[i]=h->e[up];i=up;}
    h->e[i].key=key;h->e[i].cell=cell;
}
static int PathHeapPop(pathHeap_t *h,float *key) {
    int cell=h->e[0].cell,i=0,lastCell;float last;
    *key=h->e[0].key;
    last=h->e[--h->count].key;lastCell=h->e[h->count].cell;
    for(;;) {
        int c=2*i+1;
        if(c>=h->count)break;
        if(c+1<h->count && h->e[c+1].key<h->e[c].key)c++;
        if(h->e[c].key>=last)break;
        h->e[i]=h->e[c];i=c;
    }
    if(h->count>0) {h->e[i].key=last;h->e[i].cell=lastCell;}
    return cell;
}

// Reversed arcs: every walk link and diagonal the cell search relaxes, keyed by the cell entered; edges by `to`.
static void PathRevBuild(void) {
    int c,d,n=0,k;
    if(!(pathRev.built && pathRev.topo==guideTopo && pathRev.cells==graph.count && pathRev.links==graph.links)) {
        static int count[GRAPH_MAX_CELLS];
        memset(count,0,sizeof(int)*graph.count);
        for(c=0;c<graph.count;c++)for(d=0;d<4;d++) {
            int a=graph.cell[c].link[d],b=graph.cell[c].link[(d+1)&3];
            if(a>=0)count[a]++;
            if(a>=0 && b>=0 && graph.cell[a].link[(d+1)&3]>=0 && graph.cell[a].link[(d+1)&3]==graph.cell[b].link[d])
                count[graph.cell[a].link[(d+1)&3]]++;
        }
        for(c=0;c<graph.count;c++) {pathRev.start[c]=n;n+=count[c];count[c]=pathRev.start[c];}
        pathRev.start[graph.count]=n;
        for(c=0;c<graph.count;c++)for(d=0;d<4;d++) {
            int a=graph.cell[c].link[d],b=graph.cell[c].link[(d+1)&3];
            if(a>=0) {k=count[a]++;pathRev.arc[k].from=c;pathRev.arc[k].ms=GRAPH_WALK_MS;}
            if(a>=0 && b>=0 && graph.cell[a].link[(d+1)&3]>=0 && graph.cell[a].link[(d+1)&3]==graph.cell[b].link[d]) {
                int t=graph.cell[a].link[(d+1)&3];k=count[t]++;pathRev.arc[k].from=c;pathRev.arc[k].ms=GRAPH_DIAGONAL_MS;
            }
        }
        pathRev.built=1;pathRev.topo=guideTopo;pathRev.cells=graph.count;pathRev.links=graph.links;
        pathRev.edges=0;memset(pathRev.edgeHead,-1,sizeof(int)*graph.count);
    }
    if(graph.edgeCount<pathRev.edges) {pathRev.edges=0;memset(pathRev.edgeHead,-1,sizeof(int)*graph.count);}
    for(;pathRev.edges<graph.edgeCount;pathRev.edges++) {
        int e=pathRev.edges,to=graph.edge[e].to;
        pathRev.edgeNext[e]=pathRev.edgeHead[to];pathRev.edgeHead[to]=e;
    }
}

// Dijkstra outward from the cells pushed, over the reversed arcs, for at most `budget` settled cells (0 no limit): a
// cell's time is the cheapest of its arcs to a standing cell (the goal included) plus that cell's penalty and time.
// Returns 1 when the heap is empty.
static int PathSettle(float *dist,pathHeap_t *h,int budget) {
    int settled=0;
    while(h->count>0) {
        float key,pen;int c,k,e;
        if(budget>0 && settled>=budget)return 0;
        c=PathHeapPop(h,&key);
        if(key>dist[c])continue;
        settled++;bot2_meterExpanded++;bot2_meterExpandedCell++;if(!bot2_meterInReplan)bot2_meterExpandedCellOther++;
        if(!Graph_CellStands(c))continue;   // never entered, so it leads nowhere
        pen=ClearancePenalty(c);
        for(k=pathRev.start[c];k<pathRev.start[c+1];k++) {
            int p=pathRev.arc[k].from;float t=(float)pathRev.arc[k].ms+pen+dist[c];
            if(t<dist[p]) {dist[p]=t;PathHeapPush(h,p,t);}
        }
        for(e=pathRev.edgeHead[c];e>=0;e=pathRev.edgeNext[e]) {
            const graphEdge_t *x=&graph.edge[e];float t;
            if(Graph_EdgeOff(x))continue;
            t=(float)x->costMs+pen+dist[c];
            if(t<dist[x->from]) {dist[x->from]=t;PathHeapPush(h,x->from,t);}
        }
    }
    return 1;
}

// A cell's cheapest time over its live arcs to cells not marked (the repair's seed), or PATH_UNREACHED.
static float PathOutBest(const float *dist,int c,unsigned int mark) {
    int d,e;float best=PATH_UNREACHED;
    for(d=0;d<4;d++) {
        int n=graph.cell[c].link[d];
        if(n>=0 && pathMark.stamp[n]!=mark && dist[n]<PATH_UNREACHED && Graph_CellStands(n)) {
            float t=(float)GRAPH_WALK_MS+ClearancePenalty(n)+dist[n];if(t<best)best=t;
        }
    }
    for(d=0;d<4;d++) {
        int a=graph.cell[c].link[d],b=graph.cell[c].link[(d+1)&3];
        if(a>=0 && b>=0 && graph.cell[a].link[(d+1)&3]>=0 && graph.cell[a].link[(d+1)&3]==graph.cell[b].link[d]) {
            int n=graph.cell[a].link[(d+1)&3];
            if(pathMark.stamp[n]!=mark && dist[n]<PATH_UNREACHED && Graph_CellStands(n)) {
                float t=(float)GRAPH_DIAGONAL_MS+ClearancePenalty(n)+dist[n];if(t<best)best=t;
            }
        }
    }
    for(e=graph.cell[c].firstEdge;e>=0;e=graph.edge[e].nextFrom) {
        const graphEdge_t *x=&graph.edge[e];int n=x->to;
        if(Graph_EdgeOff(x) || pathMark.stamp[n]==mark || dist[n]>=PATH_UNREACHED || !Graph_CellStands(n))continue;
        {float t=(float)x->costMs+ClearancePenalty(n)+dist[n];if(t<best)best=t;}
    }
    return best;
}

// The edge changes queued since `since`, folded into a current table as one batch. The table's times hold under
// each changed edge's cost before its first change since (pathOld; -1 no arc), so the cells whose time may have run
// through an arc made dearer or turned off are found against those costs: tight arcs from its takeoff, transitively,
// ties included. They are cleared and recomputed from their live arcs to the cells kept; the changed edges are relaxed
// at their costs now (made available or cheaper); then all of it is settled. Folding one change at a time against the
// final costs left times no route ran through (two dearer edges in a chain extracted no route).
static struct { unsigned int stamp[GRAPH_MAX_EDGES],now;int cost[GRAPH_MAX_EDGES]; } pathOld;
static int PathOldCost(int e) {return pathOld.stamp[e]==pathOld.now?pathOld.cost[e]:graph.edge[e].costMs;}
static void PathFold(float *dist,unsigned int since) {
    int head=0,tail=0,i;unsigned int q,mark;
    if(++pathOld.now==0) {memset(pathOld.stamp,0,sizeof(pathOld.stamp));pathOld.now=1;}
    for(q=since;q!=pathDecreaseSeq;q++) {
        int e=pathDecrease[q%PATH_DECREASE_RING].edge;
        if(e<0 || e>=graph.edgeCount || pathOld.stamp[e]==pathOld.now)continue;
        pathOld.stamp[e]=pathOld.now;pathOld.cost[e]=pathDecrease[q%PATH_DECREASE_RING].cost;
    }
    if(++pathMark.now==0) {memset(pathMark.stamp,0,sizeof(pathMark.stamp));pathMark.now=1;}
    mark=pathMark.now;
    for(q=since;q!=pathDecreaseSeq;q++) {
        int e=pathDecrease[q%PATH_DECREASE_RING].edge,old;const graphEdge_t *x;
        if(e<0 || e>=graph.edgeCount || !pathDecrease[q%PATH_DECREASE_RING].increase)continue;
        x=&graph.edge[e];old=pathOld.cost[e];
        if(old<0 || pathMark.stamp[x->from]==mark || dist[x->to]>=PATH_UNREACHED || dist[x->from]>=PATH_UNREACHED ||
           !Graph_CellStands(x->to))continue;
        if(fabsf(dist[x->from]-((float)old+ClearancePenalty(x->to)+dist[x->to]))>.5f)continue;
        pathStats.tight++;
        pathMark.stamp[x->from]=mark;pathMark.list[tail++]=x->from;
    }
    while(head<tail) {
        int c=pathMark.list[head++],k,f;float pen;
        if(!Graph_CellStands(c))continue;
        pen=ClearancePenalty(c);
        for(k=pathRev.start[c];k<pathRev.start[c+1];k++) {
            int p=pathRev.arc[k].from;
            if(pathMark.stamp[p]==mark || dist[p]>=PATH_UNREACHED)continue;
            if(fabsf(dist[p]-((float)pathRev.arc[k].ms+pen+dist[c]))<=.5f) {pathMark.stamp[p]=mark;pathMark.list[tail++]=p;}
        }
        for(f=pathRev.edgeHead[c];f>=0;f=pathRev.edgeNext[f]) {
            int p=graph.edge[f].from,old=PathOldCost(f);
            if(old<0 || pathMark.stamp[p]==mark || dist[p]>=PATH_UNREACHED)continue;
            if(fabsf(dist[p]-((float)old+pen+dist[c]))<=.5f) {pathMark.stamp[p]=mark;pathMark.list[tail++]=p;}
        }
    }
    for(i=0;i<tail;i++)dist[pathMark.list[i]]=PATH_UNREACHED;
    pathWork.count=0;
    for(i=0;i<tail;i++) {
        int c=pathMark.list[i];
        dist[c]=PathOutBest(dist,c,mark);
        if(dist[c]<PATH_UNREACHED)PathHeapPush(&pathWork,c,dist[c]);
    }
    for(q=since;q!=pathDecreaseSeq;q++) {
        int e=pathDecrease[q%PATH_DECREASE_RING].edge;const graphEdge_t *x;float t;
        if(e<0 || e>=graph.edgeCount)continue;
        x=&graph.edge[e];
        if(Graph_EdgeOff(x) || dist[x->to]>=PATH_UNREACHED || !Graph_CellStands(x->to))continue;
        t=(float)x->costMs+ClearancePenalty(x->to)+dist[x->to];
        if(t<dist[x->from]) {dist[x->from]=t;PathHeapPush(&pathWork,x->from,t);}
    }
    PathSettle(dist,&pathWork,0);
    pathStats.repaired+=tail;
}

// the lift cells are listed once per graph (a cell's mover is set where it is added, at the fill
// or the load, and both change graph.count or guideTopo), not found by a scan of every cell on each query.
static struct { int cell[GRAPH_MAX_CELLS],count,cells,topo,built; } moverList;
static int PathMoverStanding(int *cells) {
    int c,k,standing=0;
    if(!moverList.built || moverList.cells!=graph.count || moverList.topo!=guideTopo) {
        moverList.count=0;
        for(c=0;c<graph.count;c++)if(graph.cell[c].mover)moverList.cell[moverList.count++]=c;
        moverList.cells=graph.count;moverList.topo=guideTopo;moverList.built=1;
    }
    for(k=0;k<moverList.count;k++) {c=moverList.cell[k];standing+=Graph_CellStands(c)?(c%7919)+1:0;}
    *cells=moverList.count;
    return standing;
}

// Starts building a table for a goal in a free or the least recently used slot (never the one building).
static void PathBuildStart(int slot,int goal,int moverCells,int moverStand) {
    int i;float *dist=pathTable[slot].dist;
    pathTable[slot].goal=goal;pathTable[slot].ready=0;pathTable[slot].used=++pathTableUse;
    pathTable[slot].topo=guideTopo;pathTable[slot].full=pathFull;pathTable[slot].clearance=clearanceCells;
    pathTable[slot].cells=graph.count;pathTable[slot].links=graph.links;
    pathTable[slot].moverCells=moverCells;pathTable[slot].moverStand=moverStand;pathTable[slot].decreaseSeq=pathDecreaseSeq;
    for(i=0;i<graph.count;i++)dist[i]=PATH_UNREACHED;
    dist[goal]=0;pathBuild.count=0;PathHeapPush(&pathBuild,goal,0);
    pathBuilding=slot;pathStats.building++;
}

static int PathTableStale(int slot,int moverCells,int moverStand) {
    return pathTable[slot].topo!=guideTopo || pathTable[slot].full!=pathFull || pathTable[slot].clearance!=clearanceCells ||
        pathTable[slot].cells!=graph.count || pathTable[slot].links!=graph.links ||
        pathTable[slot].moverCells!=moverCells || pathTable[slot].moverStand!=moverStand;
}

// One frame's part of the table being built (once a frame, whichever bot runs first).
void Graph_PathTableStep(void) {
    int moverCells,moverStand,slot=pathBuilding;
    if(guidePend.slot>=0) {   // the pending guidance table's part of this frame (GuideSpreadRun keeps to the budget)
        ClearanceRead();GuideSync();
        if(guidePend.slot>=0 && guideSlot[guidePend.slot].clearance!=clearanceCells)GuidePendCancel();
        GuideSpreadRun();
    }
    if(slot<0 || pathStepTime==level.time)return;
    pathStepTime=level.time;
    PathRevBuild();ClearanceRead();
    moverStand=PathMoverStanding(&moverCells);
    if(PathTableStale(slot,moverCells,moverStand)) {PathBuildStart(slot,pathTable[slot].goal,moverCells,moverStand);return;}
    if(PathSettle(pathTable[slot].dist,&pathBuild,Bot2_WorkPerFrame(PATH_BUILD_BUDGET))) {pathTable[slot].ready=1;pathBuilding=-1;}
}

// The ready table for a goal, brought up to date with the changes queued since; NULL when there is none yet (one is
// started once the goal has been asked enough and no other is building).
static float *PathTableFor(int goal) {
    int i,slot=-1,oldest=-1,moverCells,moverStand;float *dist;
    PathRevBuild();
    moverStand=PathMoverStanding(&moverCells);
    if(pathAsks.topo!=guideTopo) {memset(pathAsks.count,0,sizeof(pathAsks.count));pathAsks.topo=guideTopo;}
    for(i=0;i<PATH_TABLES;i++) {
        if(pathTable[i].used && pathTable[i].goal==goal) {slot=i;break;}
        if(i!=pathBuilding && (oldest<0 || !pathTable[i].used || pathTable[i].used<pathTable[oldest].used))oldest=i;
    }
    pathStats.queries++;
    if(slot<0) {
        if(pathAsks.count[goal]<PATH_TABLE_ASKS) {pathAsks.count[goal]++;pathStats.searched++;return NULL;}
        if(pathBuilding<0 && oldest>=0) {pathStats.fresh++;PathBuildStart(oldest,goal,moverCells,moverStand);}
        return NULL;
    }
    pathTable[slot].used=++pathTableUse;
    if(!pathTable[slot].ready)return NULL;
    if(PathTableStale(slot,moverCells,moverStand) || pathDecreaseSeq-pathTable[slot].decreaseSeq>PATH_DECREASE_RING) {
        if(pathTable[slot].topo!=guideTopo)pathStats.topo++;
        else if(pathTable[slot].moverStand!=moverStand || pathTable[slot].moverCells!=moverCells)pathStats.mover++;
        else if(pathDecreaseSeq-pathTable[slot].decreaseSeq>PATH_DECREASE_RING)pathStats.ring++;
        else pathStats.full++;
        if(pathBuilding<0)PathBuildStart(slot,goal,moverCells,moverStand);
        else pathTable[slot].used=0;   // dropped; asked again, it is rebuilt when the builder is free
        return NULL;
    }
    dist=pathTable[slot].dist;
    if(pathTable[slot].decreaseSeq!=pathDecreaseSeq) {
        pathStats.incremental++;
        PathFold(dist,pathTable[slot].decreaseSeq);
        pathTable[slot].decreaseSeq=pathDecreaseSeq;
    } else pathStats.current++;
    if(level.time-pathStats.printed>=30000) {
        pathStats.printed=level.time;
        trap->Print("[PATHTABLE] stats level=%d queries=%d searched=%d builds=%d new_goal=%d topo=%d dearer=%d mover=%d ring=%d "
            "incremental=%d current=%d tight=%d repaired=%d\n",
            level.time,pathStats.queries,pathStats.searched,pathStats.building,pathStats.fresh,pathStats.topo,pathStats.full,
            pathStats.mover,pathStats.ring,pathStats.incremental,pathStats.current,pathStats.tight,pathStats.repaired);
    }
    return dist;
}

// bot_movePathTables (default 1; 0 off): 1 the queries below read route tables; 2 checks them,
// running the cell search as well, answering with its result and printing [PATHTABLE] when a ready table's route differs.
static int PathTablesMode(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePathTables",value,sizeof(value));
    return value[0]?atoi(value):1;
}

// A route down the table from `from`: at each cell the first arc, in the cell search's relax order, that reaches
// the least time, its cost summed forward as the search sums it.
static int PathFromTable(const float *dist,int from,int to,int *cells,int *edges,int max,int *costMs) {
    int count=0,c=from;float g=0;
    if(dist[from]>=PATH_UNREACHED)return 0;
    cells[0]=from;edges[0]=-1;count=1;
    while(c!=to) {
        int d,e,best=-1,bestEdge=-1;float bestT=PATH_UNREACHED,bestCost=0;
        if(count>graph.count)return 0;
        if(count>=max) {   // the first max cells, priced as the whole route
            PathTruncated("table",from,to,count+1,max);
            if(costMs)*costMs=(int)(g+dist[c]);
            return count;
        }
        for(d=0;d<4;d++) {
            int n=graph.cell[c].link[d];
            if(n>=0 && Graph_CellStands(n)) {
                float t=(float)GRAPH_WALK_MS+ClearancePenalty(n)+dist[n];
                if(t<bestT) {bestT=t;best=n;bestEdge=-1;bestCost=(float)GRAPH_WALK_MS;}
            }
        }
        for(d=0;d<4;d++) {
            int a=graph.cell[c].link[d],b=graph.cell[c].link[(d+1)&3];
            if(a>=0 && b>=0 && graph.cell[a].link[(d+1)&3]>=0 && graph.cell[a].link[(d+1)&3]==graph.cell[b].link[d]) {
                int n=graph.cell[a].link[(d+1)&3];
                if(Graph_CellStands(n)) {
                    float t=(float)GRAPH_DIAGONAL_MS+ClearancePenalty(n)+dist[n];
                    if(t<bestT) {bestT=t;best=n;bestEdge=-1;bestCost=(float)GRAPH_DIAGONAL_MS;}
                }
            }
        }
        for(e=graph.cell[c].firstEdge;e>=0;e=graph.edge[e].nextFrom) {
            const graphEdge_t *x=&graph.edge[e];int n=x->to;
            if(Graph_EdgeOff(x) || !Graph_CellStands(n))continue;
            {
                float t=(float)x->costMs+ClearancePenalty(n)+dist[n];
                if(t<bestT) {bestT=t;best=n;bestEdge=e;bestCost=(float)x->costMs;}
            }
        }
        if(best<0 || bestT>=PATH_UNREACHED)return 0;
        g=g+bestCost+ClearancePenalty(best);
        cells[count]=best;edges[count]=bestEdge;count++;c=best;
    }
    if(costMs)*costMs=(int)g;
    return count;
}

static struct { int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH]; } pathCheck;

int Graph_PathTable(int from,int to,int *cells,int *edges,int max,int *costMs) {
    int mode=PathTablesMode(),count,searched,searchCost=0,cost=0;float *dist;double began;
    planBound.valid=0;
    if(!mode || bodies.count || forbid.on || corridor.count>0 || from<0 || to<0 || from>=graph.count || to>=graph.count || max<1 || from==to)
        return Graph_Path(from,to,cells,edges,max,costMs);
    began=Bot2_MoveCostClock();
    OffExpire();ClearanceRead();
    dist=PathTableFor(to);
    if(!dist)return Graph_Path(from,to,cells,edges,max,costMs);
    if(mode==2) {
        count=PathFromTable(dist,from,to,pathCheck.cells,pathCheck.edges,max,&cost);
        searched=Graph_Path(from,to,cells,edges,max,&searchCost);
        if(count!=searched || (count && (cost!=searchCost || memcmp(pathCheck.cells,cells,sizeof(int)*count))))
            trap->Print("[PATHTABLE] level=%d from=%d to=%d table_count=%d table_cost=%d search_count=%d search_cost=%d same_cells=%d\n",
                level.time,from,to,count,count?cost:-1,searched,searched?searchCost:-1,
                count==searched && count && !memcmp(pathCheck.cells,cells,sizeof(int)*count));
        if(costMs && searched)*costMs=searchCost;
        return searched;
    }
    count=PathFromTable(dist,from,to,cells,edges,max,&cost);
    if(costMs && count)*costMs=cost;
    Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_GRAPHPATH,began);
    return count;
}

// A step's price in the reroute pass's walk model: its 2D length at 250 u/s, plus 250 ms when it is an edge.
int Graph_PriceMs(int from,int to,int edge) {
    float dx=graph.cell[to].ix*(float)GRAPH_GRID-graph.cell[from].ix*(float)GRAPH_GRID;
    float dy=graph.cell[to].iy*(float)GRAPH_GRID-graph.cell[from].iy*(float)GRAPH_GRID;
    return (int)(sqrtf(dx*dx+dy*dy)/250*1000)+(edge>=0?250:0);
}

// bot_movePlanGuide (default 1, records/2026-10-30-hop-room-offline.md; 0 off): 1 the state search
// uses the guidance tables above; 2 checks them, running the search both ways, keeping the straight line's result and
// printing [PLANGUIDE] after both have returned.
static int GuideMode(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePlanGuide",value,sizeof(value));
    return value[0]?atoi(value):1;
}

static struct { int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];float speeds[GRAPH_MAX_PATH]; } guideCheck;

int Graph_PlanState(int from,const graphState_t *start,int to,const int *avoid,int avoidCount,int require,
                    int *cells,int *edges,float *speeds,int max,int *costMs,int *expanded,int *overflow) {
    double began=Bot2_MoveCostClock();int count,mode=GuideMode();
    int longN=mode==1?PlanCvar("bot_movePlanGuideLong"):0,boundN=mode!=2?PlanCvar("bot_movePlanBound"):0;
    OffExpire();planBound.valid=0;
    if(mode) {char value[16];trap->Cvar_VariableStringBuffer("bot_movePlanGuideMax",value,sizeof(value));guideMax=value[0]?atoi(value)!=0:1;}
    if(mode==1) {
        int wanted=GuideWanted(from,to),slot;
        if(!wanted && longN>0)wanted=GuideHeld(to);
        slot=wanted?GuideFor(to,start->speedMs>0):-1;
        if(slot>=0) {
            guideActive[0]=GuideTable(slot,0);guideActive[1]=GuideTable(slot,1);guideActiveFresh=guideSlot[slot].seen==guideLog.seq;
        }
        if(longN>0 || boundN>0)
            count=PlanStateStaged(from,start,to,avoid,avoidCount,require,cells,edges,speeds,max,costMs,expanded,overflow,
                                  longN,slot<0?start->speedMs>0:-1,boundN);
        else count=PlanStateSearch(from,start,to,avoid,avoidCount,require,cells,edges,speeds,max,costMs,expanded,overflow);
        guideActive[0]=guideActive[1]=NULL;
    } else if(mode==2) {
        double t0=Bot2_ClockUs(),t1,t2,t3;int slot,wanted,guidedCount=0,guidedCost=0,guidedExpanded=0,guidedOverflow=0,same=-1;
        float hRoot=-1;
        count=PlanStateSearch(from,start,to,avoid,avoidCount,require,cells,edges,speeds,max,costMs,expanded,overflow);
        t1=Bot2_ClockUs();
        wanted=GuideWanted(from,to);
        slot=wanted?GuideFor(to,start->speedMs>0):-1;
        t2=Bot2_ClockUs();
        if(slot>=0) {
            graphState_t s=*start;
            if(s.speed<0)s.speed=0;
            guideActive[0]=GuideTable(slot,0);guideActive[1]=GuideTable(slot,1);guideActiveFresh=guideSlot[slot].seen==guideLog.seq;
            hRoot=StateHeuristic(from,&s,to);
            {
                static float kept[GRAPH_MAX_PATH];int keptCount=stateTimes.count;
                memcpy(kept,stateTimes.g,sizeof(float)*keptCount);
                guidedCount=PlanStateSearch(from,start,to,avoid,avoidCount,require,guideCheck.cells,guideCheck.edges,
                                            guideCheck.speeds,max,&guidedCost,&guidedExpanded,&guidedOverflow);
                memcpy(stateTimes.g,kept,sizeof(float)*keptCount);stateTimes.count=keptCount;
            }
            guideActive[0]=guideActive[1]=NULL;
            same=guidedCount==count && (!count || (!memcmp(guideCheck.cells,cells,sizeof(int)*count) &&
                                                   !memcmp(guideCheck.edges,edges,sizeof(int)*count)));
        }
        t3=Bot2_ClockUs();
        trap->Print("[PLANGUIDE] level=%d from=%d to=%d speed=%.1f speed_ms=%d require=%d count=%d cost=%d expanded=%d "
            "overflow=%d us=%.0f slot=%d build_us=%.0f g_count=%d g_cost=%d g_expanded=%d g_overflow=%d g_us=%.0f "
            "same=%d h_root=%.1f h_line=%.1f wanted=%d pinned=%d fresh=%d arcs=%d arcs_overflow=%d rebuilds=%d builds=%d "
            "folds=%d capped=%d preloaded=%d\n",
            level.time,from,to,start->speed,start->speedMs,require,count,count?*costMs:-1,*expanded,*overflow,t1-t0,
            slot,t2-t1,guidedCount,guidedCount?guidedCost:-1,guidedExpanded,guidedOverflow,t3-t2,same,hRoot,
            from>=0 && to>=0 && from<graph.count && to<graph.count?HeuristicOf(from,to):-1.0f,wanted,
            slot>=0?guideSlot[slot].pinned:0,slot>=0?guideSlot[slot].seen==guideLog.seq:0,
            GuideLiveArcs(),guideArcs.overflow,guideStats.rebuilds,guideStats.builds,guideStats.folds,
            guideStats.capped,guideStats.preloaded);
    } else if(boundN>0)
        count=PlanStateStaged(from,start,to,avoid,avoidCount,require,cells,edges,speeds,max,costMs,expanded,overflow,0,-1,boundN);
    else count=PlanStateSearch(from,start,to,avoid,avoidCount,require,cells,edges,speeds,max,costMs,expanded,overflow);
    Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_PLANSTATE,began);
    Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_PLANSTATE_CALLS);
    Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_PLANSTATE_EXPANDED,*expanded);
    Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_PLANSTATE_OVERFLOW,*overflow?1:0);
    return count;
}

// State searches split over frames (bot_movePlanSplit, option 2a, records/2026-10-06-frame-cost.md). A search runs
// in a table of its own for a budget of expansions a call, so a cross-map replan (about 28,000 expansions on Kejim)
// costs a few milliseconds a frame instead of 85 ms in one. Between its parts the graph may learn or lose edges, as
// it may between two replans; a rebuilt or reloaded graph ends it. The bodies and avoided edges are the ones it
// began with. Budgets are expansion counts, so seeded worlds replay.
static graphStateSearch_t stateSearchSplit[GRAPH_PLAN_SPLITS];
static struct {
    int id;                     // 0 free
    unsigned int topo;          // guideTopo at the start: a rebuild, reload or learned-edge reset ends the search
    int count;vec3_t at[GRAPH_MAX_BODIES];
    int forbidOn;vec3_t forbidMins,forbidMaxs;   // the forbidden volume it began with
} planSplit[GRAPH_PLAN_SPLITS];
static int planSplitSeq;

static int SplitSlot(int id) {
    int i=id>0?(id-1)%GRAPH_PLAN_SPLITS:-1;
    return i>=0 && planSplit[i].id==id?i:-1;
}

void Graph_PlanSplitCancel(int id) {
    int i=SplitSlot(id);
    if(i>=0)planSplit[i].id=0;
}

// Runs one part in the split's table under its bodies; the caller's bodies are put back.
static int SplitRun(int i,int budget) {
    graphStateSearch_t *saved=stateSearchAt;int savedCount=bodies.count,done,savedForbid=forbid.on;vec3_t savedAt[GRAPH_MAX_BODIES],savedMins,savedMaxs;
    double began=Bot2_MoveCostClock();
    memcpy(savedAt,bodies.at,sizeof(savedAt));VectorCopy(forbid.mins,savedMins);VectorCopy(forbid.maxs,savedMaxs);
    Graph_BodiesSet((const vec3_t *)planSplit[i].at,planSplit[i].count);
    Graph_ForbidSet(planSplit[i].forbidOn?planSplit[i].forbidMins:NULL,planSplit[i].forbidMaxs);
    stateSearchAt=&stateSearchSplit[i];planBound.valid=0;
    OffExpire();ClearanceRead();
    if(!stateSearch.done) {StateAvoidMark(stateSearch.avoid,stateSearch.avoidCount);heuristicMemo.goal=-1;}
    done=StateRun(budget);
    stateSearchAt=saved;
    Graph_BodiesSet((const vec3_t *)savedAt,savedCount);
    Graph_ForbidSet(savedForbid?savedMins:NULL,savedMaxs);
    Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_PLANSTATE,began);
    return done;
}

int Graph_PlanSplitStart(int from,const graphState_t *start,int to,const int *avoid,int avoidCount,int budget,int *id) {
    int i,began;graphStateSearch_t *saved=stateSearchAt;
    *id=0;
    if(budget<=0 || GuideMode() || from<0 || to<0 || from>=graph.count || to>=graph.count)return 0;
    for(i=0;i<GRAPH_PLAN_SPLITS && planSplit[i].id;i++);
    if(i>=GRAPH_PLAN_SPLITS)return 0;
    if(++planSplitSeq>100000000)planSplitSeq=1;   // ids stay unique, so a stale one finds no slot
    planSplit[i].id=planSplitSeq*GRAPH_PLAN_SPLITS+i+1;
    planSplit[i].topo=guideTopo;planSplit[i].count=bodies.count;
    memcpy(planSplit[i].at,bodies.at,sizeof(planSplit[i].at));
    planSplit[i].forbidOn=forbid.on;VectorCopy(forbid.mins,planSplit[i].forbidMins);VectorCopy(forbid.maxs,planSplit[i].forbidMaxs);
    stateSearchAt=&stateSearchSplit[i];
    ClearanceRead();
    began=StateBegin(from,start,to,avoid,avoidCount,-1);
    stateSearchAt=saved;
    if(!began)stateSearchSplit[i].done=1;
    *id=planSplit[i].id;
    Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_PLANSTATE_CALLS);
    return Graph_PlanSplitResume(*id,budget);
}

int Graph_PlanSplitResume(int id,int budget) {
    int i=SplitSlot(id);
    if(i<0)return -1;
    if(planSplit[i].topo!=guideTopo) {planSplit[i].id=0;return -1;}
    return SplitRun(i,budget);
}

int Graph_PlanSplitResult(int id,int *cells,int *edges,float *speeds,int max,int *costMs,int *expanded,int *overflow) {
    int i=SplitSlot(id),count;graphStateSearch_t *saved=stateSearchAt;
    *expanded=0;*overflow=0;
    if(i<0 || !stateSearchSplit[i].done)return 0;
    stateSearchAt=&stateSearchSplit[i];
    count=StateResult(cells,edges,speeds,max,costMs,expanded,overflow);
    stateSearchAt=saved;
    planSplit[i].id=0;
    Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_PLANSTATE_EXPANDED,*expanded);
    Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_PLANSTATE_OVERFLOW,*overflow?1:0);
    return count;
}

// ---------------------------------------------------------------------------
// Cache and report.
// ---------------------------------------------------------------------------
static void Binding(void) {
    trap->Cvar_VariableStringBuffer("mapname",graph.map,sizeof(graph.map));
    trap->Cvar_VariableStringBuffer("sv_mapChecksum",graph.checksum,sizeof(graph.checksum));
    graph.gravity=g_gravity.integer;graph.speed=g_speed.integer;
}

// bot2-graph/<map>_<checksum>: the name a map's cache, seed and guide goal list share.
static void CacheBase(char *path,int size) {
    char name[64];int i;
    Q_strncpyz(name,graph.map,sizeof(name));
    for(i=0;name[i];i++)if(name[i]=='/' || name[i]=='\\')name[i]='_';
    Com_sprintf(path,size,"bot2-graph/%s_%s",name,graph.checksum);
}

// The cache file. Load refuses a file on its version, grid and physics as well as its map
// and checksum, and every refusal used to fall through to a build saved over the file it could not read: one server
// started with another g_speed replaced a 94,711-edge seed with an unseeded build. Now:
// - the physics binding is in the name when it is not the default (g_gravity 800, g_speed 250, which keep the plain
//   name every seed and tool uses): two physics settings keep two caches;
// - a file that exists and does not load is never written over: the graph loads from, or is saved to, the slot
//   beside it (<name>.rebuilt.txt), and with both unreadable nothing is saved (cache_unreadable / cache_write_skipped);
// - a save writes <file>.tmp, then the file, then empties the .tmp, so a kill or a full drive mid-write leaves one
//   complete copy; a load that finds the file unreadable and the .tmp whole takes the .tmp (an interrupted save).
// cacheTarget is the file this map's saves go to, set by Move_PrepareMap's load.
static char cacheTarget[MAX_QPATH];
static void CachePath(char *path,int size,int slot) {
    CacheBase(path,size);
    if(graph.gravity!=800 || graph.speed!=250)Q_strcat(path,size,va("_g%d_s%d",graph.gravity,graph.speed));
    Q_strcat(path,size,slot?".rebuilt.txt":".txt");
}

static void WriteLine(fileHandle_t file,const char *line) {trap->FS_Write(line,strlen(line),file);}

// The writer, over a given edge array: the live graph's for an explicit save,
// the snapshot's for a deferred one. Cells, nodes and every other header
// field are read from the graph, which holds them fixed after the load.
// One cell record. A cell on a mover's floor appends the mover's model and
// position; a world cell's record is unchanged, so a graph with no mover
// cells keeps its cache text and its identity.
static void CellLine(char *line,int size,const graphCell_t *c) {
    Com_sprintf(line,size,"c %d %d %.3f %d %d %d %d %d %d %d %d %d",c->ix,c->iy,c->z,
        c->link[0],c->link[1],c->link[2],c->link[3],c->refused,c->flags,c->seed,c->node[0],c->node[1]);
    if(c->mover)Q_strcat(line,size,va(" %d %d",c->mover,c->moverState));
    Q_strcat(line,size,"\n");
}

static qboolean WriteCache(const char *path,const graphEdge_t *edges,int edgeCount,int capped) {
    char line[GRAPH_CACHE_LINE];fileHandle_t file=0;int i;
    trap->FS_Open(path,&file,FS_WRITE);
    if(!file)return qfalse;
    Com_sprintf(line,sizeof(line),"graph %d map=%s checksum=%s grid=%d gravity=%d speed=%d step=%d cells=%d nodes=%d edges=%d seeds=%d seed_refused=%d samples=%d refusals=%d links=%d refined=%d boundary=%d capped=%d hints=%d hints_bound=%d\n",
        GRAPH_VERSION,graph.map,graph.checksum,GRAPH_GRID,graph.gravity,graph.speed,STEPSIZE,graph.count,graph.nodeCount,edgeCount,
        graph.seeds,graph.seedRefused,graph.samples,graph.refusals,graph.links,graph.refined,graph.boundary,capped,graph.hints,graph.hintsBound);
    WriteLine(file,line);
    for(i=0;i<GRAPH_REASONS;i++) {
        Com_sprintf(line,sizeof(line),"r %s %d\n",graphReasons[i],graph.reason[i]);WriteLine(file,line);
    }
    for(i=0;i<graph.count;i++) {
        const graphCell_t *c=&graph.cell[i];
        CellLine(line,sizeof(line),c);
        WriteLine(file,line);
    }
    for(i=0;i<graph.nodeCount;i++) {
        const graphNode_t *n=&graph.node[i];
        Com_sprintf(line,sizeof(line),"n %d %d %d %.3f %.3f %.1f %d\n",n->axis,n->first,n->count,n->zMin,n->zMax,n->width,n->flags);
        WriteLine(file,line);
    }
    for(i=0;i<edgeCount;i++) {
        const graphEdge_t *e=&edges[i];
        Com_sprintf(line,sizeof(line),"e %d %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.6f %.6f %d %d %d %.1f %.6f %.6f %.1f %.6f %.6f\n",e->type,e->from,e->to,
            e->takeoff[0],e->takeoff[1],e->takeoff[2],e->landing[0],e->landing[1],e->landing[2],e->dir[0],e->dir[1],e->costMs,e->validated,
            e->tap,e->entrySpeed,e->entryDir[0],e->entryDir[1],e->exitSpeed,e->exitDir[0],e->exitDir[1]);
        WriteLine(file,line);
    }
    trap->FS_Close(file);
    return qtrue;
}

// The save: <target>.tmp in full, then the target, then the .tmp emptied. The game's file API has no
// rename, so the swap is two writes; between them either file is whole. A map whose cache could not be read and
// whose rebuilt slot could not be read either has no target and saves nothing.
static qboolean SaveEdges(const graphEdge_t *edges,int edgeCount,int capped) {
    char tmp[MAX_QPATH];fileHandle_t file=0;
    if(!graph.map[0])return qfalse;
    if(!cacheTarget[0]) {trap->Print("[GRAPH] cache_write_skipped reason=unreadable\n");return qfalse;}
    Com_sprintf(tmp,sizeof(tmp),"%s.tmp",cacheTarget);
    if(!WriteCache(tmp,edges,edgeCount,capped) || !WriteCache(cacheTarget,edges,edgeCount,capped))return qfalse;
    trap->FS_Open(tmp,&file,FS_WRITE);
    if(file)trap->FS_Close(file);
    return qtrue;
}

// An explicit save of the live graph. It supersedes any pending snapshot,
// which is older, exactly as today's explicit write superseded the autosaves
// before it.
qboolean Graph_Save(void) {
    saveSnapshot.pending=0;saveSnapshot.requests=0;
    return SaveEdges(graph.edge,graph.edgeCount,graph.capped);
}

// ---------------------------------------------------------------------------
// The graph's own identity. A tape names cells and edges by index, so it means
// nothing on a different graph, and the map checksum cannot tell two graphs
// apart: an early research seed and the unseeded build of the same map
// share map and checksum and differ in 94,711 edges. The digest is taken over
// the cell and edge records exactly as Graph_Save writes them, so the cache a
// build wrote and the load that read it back agree by construction (%.3f and
// %.6f round-trip through text), and over the version, grid and physics
// binding the cache header already refuses on.
//
// Frozen after the load or the build. Edges learned while a world runs append
// to the array and do not change it: the identity a plan is bound to is the
// graph it was authored against, and appending never moves an existing index.
// ---------------------------------------------------------------------------
// FNV-1a over 64 bits, carried as two 32-bit halves so the build needs no
// 64-bit integer type. The prime is 0x100000001b3, which is 2^40 + 0x1b3, so
// the product's low 64 bits are (value * 0x1b3) plus (value << 40). The first
// term's low half is accumulated through 16-bit chunks, so nothing overflows
// 32 bits, and its carry and the high half wrap in unsigned arithmetic, which
// is what "the low 64 bits" means. The second term shifts the value past bit
// 63 except for its low half, which lands in the high word shifted by 8.
// Checked against a reference 64-bit FNV-1a on 2,000 strings.
static void DigestBytes(unsigned int *high,unsigned int *low,const char *text) {
    unsigned int h=*high,l=*low;
    for(;*text;text++) {
        unsigned int a0,a1,t0,t1;
        l^=(unsigned char)*text;
        a0=l&0xffffu;a1=(l>>16)&0xffffu;
        t0=a0*0x1b3u;
        t1=a1*0x1b3u+(t0>>16);
        h=(t1>>16)+(l<<8)+h*0x1b3u;
        l=(t0&0xffffu)|((t1&0xffffu)<<16);
    }
    *high=h;*low=l;
}

static void Identity(void) {
    unsigned int high=0xcbf29ce4u,low=0x84222325u;   // FNV-1a 64 offset basis
    char line[GRAPH_CACHE_LINE];int i;
    Com_sprintf(line,sizeof(line),"graph %d grid=%d gravity=%d speed=%d step=%d cells=%d nodes=%d edges=%d\n",
        GRAPH_VERSION,GRAPH_GRID,graph.gravity,graph.speed,STEPSIZE,graph.count,graph.nodeCount,graph.edgeCount);
    DigestBytes(&high,&low,line);
    for(i=0;i<graph.count;i++) {
        const graphCell_t *c=&graph.cell[i];
        CellLine(line,sizeof(line),c);
        DigestBytes(&high,&low,line);
    }
    for(i=0;i<graph.edgeCount;i++) {
        const graphEdge_t *e=&graph.edge[i];
        Com_sprintf(line,sizeof(line),"e %d %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.6f %.6f %d %d %d %.1f %.6f %.6f %.1f %.6f %.6f\n",e->type,e->from,e->to,
            e->takeoff[0],e->takeoff[1],e->takeoff[2],e->landing[0],e->landing[1],e->landing[2],e->dir[0],e->dir[1],e->costMs,e->validated,
            e->tap,e->entrySpeed,e->entryDir[0],e->entryDir[1],e->exitSpeed,e->exitDir[0],e->exitDir[1]);
        DigestBytes(&high,&low,line);
    }
    Com_sprintf(graph.identity,sizeof(graph.identity),"g1-%08x%08x",high,low);
}

const char *Graph_Identity(void) {return graph.identity[0]?graph.identity:"none";}

// Load a cache written by this version for this map, checksum and physics
// A loaded edge record beyond its cells: a known type, a positive cost (a zero or negative
// one breaks the route tables' Dijkstra, whose heap is sized for one push per arc), finite floats (sscanf takes
// "nan"), and a lift's inline model in range. A valid seed passes untouched (Kejim's costs run 50-3,787 ms).
static qboolean Finite(float x) {return x==x && x<1e9f && x>-1e9f;}
static qboolean EdgeRecordValid(int type,int costMs,int tap,const vec3_t takeoff,const vec3_t landing,const float *dir,
                                float entrySpeed,const float *entryDir,float exitSpeed,const float *exitDir) {
    int i;
    if(type<=GRAPH_EDGE_NONE || type>=GRAPH_EDGE_TYPES || costMs<1)return qfalse;
    if(type==GRAPH_EDGE_LIFT && (tap<1 || tap>=MAX_MODELS))return qfalse;
    for(i=0;i<3;i++)if(!Finite(takeoff[i]) || !Finite(landing[i]))return qfalse;
    for(i=0;i<2;i++)if(!Finite(dir[i]) || !Finite(entryDir[i]) || !Finite(exitDir[i]))return qfalse;
    return Finite(entrySpeed) && Finite(exitSpeed);
}

// binding. Anything else, or any malformed line, is a rebuild. *why names the refusal: "absent" (no file, or an
// empty one), else why a file that is there was not taken.
static qboolean Load(const char *path,const char **why) {
    char map[64],checksum[64];fileHandle_t file=0;int size,version,grid,gravity,speed,step,cells,nodes,edges;
    char *bytes,*line,*next;int cell=0,node=0,edge=0,ok=1,badEdge=0;
    *why="absent";
    size=trap->FS_Open(path,&file,FS_READ);
    if(!file)return qfalse;
    if(size<=0 || size>GRAPH_CACHE_MAX_BYTES) {trap->FS_Close(file);if(size>0)*why="size";return qfalse;}
    bytes=(char*)malloc(size+1);
    if(!bytes) {trap->FS_Close(file);*why="malloc";return qfalse;}
    trap->FS_Read(bytes,size,file);trap->FS_Close(file);bytes[size]=0;
    // sscanf implementations may inspect the whole input string. Bound each
    // call to one record instead of repeatedly scanning the remaining cache.
    next=strchr(bytes,'\n');if(next)*next++=0;
    if(sscanf(bytes,"graph %d map=%63s checksum=%63s grid=%d gravity=%d speed=%d step=%d cells=%d nodes=%d edges=%d seeds=%d seed_refused=%d samples=%d refusals=%d links=%d refined=%d boundary=%d capped=%d hints=%d hints_bound=%d",
              &version,map,checksum,&grid,&gravity,&speed,&step,&cells,&nodes,&edges,&graph.seeds,&graph.seedRefused,
              &graph.samples,&graph.refusals,&graph.links,&graph.refined,&graph.boundary,&graph.capped,&graph.hints,&graph.hintsBound)!=20 ||
       version!=GRAPH_VERSION || strcmp(map,graph.map) || strcmp(checksum,graph.checksum) || grid!=GRAPH_GRID ||
       gravity!=graph.gravity || speed!=graph.speed || step!=STEPSIZE || cells<0 || cells>GRAPH_MAX_CELLS || nodes<0 || nodes>GRAPH_MAX_NODES ||
       edges<0 || edges>GRAPH_MAX_EDGES) {
        *why=version!=GRAPH_VERSION?"version":gravity!=graph.gravity || speed!=graph.speed?"physics":"header";
        free(bytes);return qfalse;
    }
    for(line=next;line && ok;line=next) {
        next=strchr(line,'\n');if(next)*next++=0;
        if(line[0]=='r') {
            char name[40];int count;
            if(sscanf(line,"r %39s %d",name,&count)==2)graph.reason[ReasonIndex(name)]=count;else ok=0;
        } else if(line[0]=='c') {
            int ix,iy,l0,l1,l2,l3,refused,flags,seed,n0,n1,index,mover=0,moverState=0,fields;float z;
            fields=sscanf(line,"c %d %d %f %d %d %d %d %d %d %d %d %d %d %d",&ix,&iy,&z,&l0,&l1,&l2,&l3,&refused,&flags,&seed,&n0,&n1,&mover,&moverState);
            if((fields!=12 && fields!=14) || (fields==14 && mover<=0) ||
               cell>=cells || (index=AddCell(ix,iy,z,seed))!=cell) {ok=0;break;}
            graph.cell[index].mover=(short)mover;graph.cell[index].moverState=(unsigned char)moverState;
            graph.cell[index].link[0]=l0;graph.cell[index].link[1]=l1;graph.cell[index].link[2]=l2;graph.cell[index].link[3]=l3;
            graph.cell[index].refused=refused;graph.cell[index].flags=flags;graph.cell[index].node[0]=n0;graph.cell[index].node[1]=n1;
            cell++;
        } else if(line[0]=='n') {
            graphNode_t *n=&graph.node[node];int axis,first,count,flags;
            if(node>=nodes || sscanf(line,"n %d %d %d %f %f %f %d",&axis,&first,&count,&n->zMin,&n->zMax,&n->width,&flags)!=7) {ok=0;break;}
            n->axis=axis;n->first=first;n->count=count;n->flags=flags;node++;graph.nodeCount=node;
        } else if(line[0]=='e') {
            int type,from,to,costMs,validated,tap;vec3_t takeoff,landing;float dir[2],entrySpeed,entryDir[2],exitSpeed,exitDir[2];
            if(edge>=edges || cell!=cells ||
               sscanf(line,"e %d %d %d %f %f %f %f %f %f %f %f %d %d %d %f %f %f %f %f %f",&type,&from,&to,&takeoff[0],&takeoff[1],&takeoff[2],
                      &landing[0],&landing[1],&landing[2],&dir[0],&dir[1],&costMs,&validated,
                      &tap,&entrySpeed,&entryDir[0],&entryDir[1],&exitSpeed,&exitDir[0],&exitDir[1])!=20) {ok=0;break;}
            if(!EdgeRecordValid(type,costMs,tap,takeoff,landing,dir,entrySpeed,entryDir,exitSpeed,exitDir)) {ok=0;badEdge=1;break;}
            if(AddEdgeRecord(type,from,to,takeoff,landing,dir,costMs,validated)!=edge) {ok=0;break;}
            SetStrafeState(&graph.edge[edge],tap,entrySpeed,entryDir,exitSpeed,exitDir);
            edge++;
        } else if(line[0] && line[0]!='\r')ok=0;
    }
    free(bytes);
    if(ok && cell==cells && node==nodes && edge==edges) {
        int i,d;
        for(i=0;i<graph.count;i++) {
            for(d=0;d<4;d++)if(graph.cell[i].link[d]>=graph.count)ok=0;
            if(graph.cell[i].node[0]>=graph.nodeCount || graph.cell[i].node[1]>=graph.nodeCount)ok=0;
        }
        for(i=0;i<graph.nodeCount;i++)if(graph.node[i].first<0 || graph.node[i].first>=graph.count)ok=0;
    } else ok=0;
    if(!ok) {Reset();Binding();*why=badEdge?"edge_invalid":"malformed";return qfalse;}
    graph.cached=1;
    return qtrue;
}

// Load the map's cache: the file, else its .tmp when the file is unreadable (an interrupted save), then the
// same for the rebuilt slot; sets cacheTarget to the slot the graph's saves go to. `accept` refuses a graph that
// loaded but must be rebuilt (a pre-lift cache on a lift map); that slot is then kept as unreadable.
static qboolean LoadCache(qboolean (*accept)(void)) {
    int slot;cacheTarget[0]=0;
    for(slot=0;slot<2;slot++) {
        char path[MAX_QPATH],tmp[MAX_QPATH];const char *why,*whyTmp;
        CachePath(path,sizeof(path),slot);Com_sprintf(tmp,sizeof(tmp),"%s.tmp",path);
        if(Load(path,&why) || (strcmp(why,"absent") && Load(tmp,&whyTmp))) {
            if(!accept || accept()) {Q_strncpyz(cacheTarget,path,sizeof(cacheTarget));return qtrue;}
            why="pre_lift";Reset();Binding();
        }
        if(!strcmp(why,"absent")) {Q_strncpyz(cacheTarget,path,sizeof(cacheTarget));return qfalse;}
        trap->Print("[GRAPH] cache_unreadable file=%s reason=%s kept=1\n",path,why);
    }
    return qfalse;   // both slots there and unreadable: build, save nothing
}

static void Report(const char *event) {
    char reasons[512];int i,edgesByType[GRAPH_EDGE_TYPES]={0,0,0,0,0,0,0,0},moverCells=0;
    for(i=0;i<graph.count;i++)if(graph.cell[i].mover)moverCells++;
    reasons[0]=0;
    for(i=0;i<GRAPH_REASONS;i++)
        Q_strcat(reasons,sizeof(reasons),va("%s%s=%d",i?" ":"",graphReasons[i],graph.reason[i]));
    for(i=0;i<graph.edgeCount;i++)if(graph.edge[i].type>=0 && graph.edge[i].type<GRAPH_EDGE_TYPES)edgesByType[graph.edge[i].type]++;
    trap->Print("[GRAPH] %s map=%s checksum=%s graph=%s cached=%d seeds=%d seed_refused=%d cells=%d nodes=%d links=%d refined=%d boundary=%d refused=%d samples=%d "
                "edges=%d hop=%d pad=%d drop=%d jump=%d strafe=%d climb=%d hints=%d hints_bound=%d capped=%d clients=%d elapsed_ms=%d mover_cells=%d lift=%d\n",
        event,graph.map,graph.checksum,Graph_Identity(),graph.cached,graph.seeds,graph.seedRefused,graph.count,graph.nodeCount,graph.links,graph.refined,
        graph.boundary,graph.refusals,graph.samples,graph.edgeCount,edgesByType[GRAPH_EDGE_HOP],edgesByType[GRAPH_EDGE_PAD],
        edgesByType[GRAPH_EDGE_DROP],edgesByType[GRAPH_EDGE_JUMP],edgesByType[GRAPH_EDGE_STRAFE],edgesByType[GRAPH_EDGE_CLIMB],graph.hints,graph.hintsBound,graph.capped,
        level.numConnectedClients,graph.elapsedMs,moverCells,edgesByType[GRAPH_EDGE_LIFT]);
    trap->Print("[GRAPH] refusals %s\n",reasons);
}

static void Build(void) {
    int began=trap->Milliseconds();
    Reset();Binding();
    Seeds();Fill();Nodes();
    if(!Graph_NativeEdges())Hints();
    else {
        Pads();DropsAndJumps();
        trap->Print("[GRAPH] native pads=%d drops=%d jumps=%d\n",graph.nativePads,graph.nativeDrops,graph.nativeJumps);
    }
    if(ClimbsMode()>0)Climbs();
    Lifts();
    graph.elapsedMs=trap->Milliseconds()-began;
}

// A cache from before lifts has no cells on a mover's floor: the fill then refused them, and every such cell
// defaults to world floor on load, so no ride can ever be found. On a map with lift calls such a cache is rebuilt
// (into the rebuilt slot, the old file kept). Kejim and Dantooine v3 have no lift calls, so their caches,
// seeds and identities are untouched; a rebuilt graph that still has no mover cell is rebuilt again at each load,
// which costs the build and the learned edges saved since, never a route.
static qboolean PostLift(void) {
    int i;
    for(i=0;i<graph.count;i++)if(graph.cell[i].mover)return qtrue;
    if(Bot2_MoverCalls()<=0)return qtrue;
    trap->Print("[GRAPH] cache_pre_lift calls=%d rebuilding\n",Bot2_MoverCalls());
    return qfalse;
}

qboolean Move_PrepareMap(void) {
    int began;qboolean loaded;
    // A snapshot the previous map left pending is written before the graph
    // it describes is reset. Nothing is pending unless the deferral is on.
    Graph_SaveFlush();
    Bot2_MoverReset();
    began=trap->Milliseconds();
    Reset();Binding();pathTruncatedLogged=0;
    loaded=LoadCache(PostLift);
    if(!loaded) {
        Build();
        if(!Graph_Save())trap->Print("[GRAPH] cache_write_failed\n");
    } else {
        // A cache written with climbs keeps them; bot_moveClimbs 0 leaves
        // them out of the search for this map, so a no-climb arm on the same
        // cache is one. The flag is not saved and the identity does not read
        // it, so the cache and its binding are unchanged.
        int i,have=0,off=ClimbsMode()<=0;
        for(i=0;i<graph.edgeCount;i++)if(graph.edge[i].type==GRAPH_EDGE_CLIMB) {have=1;if(off)graph.edge[i].disabled=1;}
        if(ClimbsMode()>=2 && !have)Climbs();
        // A cache from before rides has lift cells and no lift edges.
        for(i=0,have=0;i<graph.edgeCount;i++)if(graph.edge[i].type==GRAPH_EDGE_LIFT)have=1;
        for(i=0;i<graph.count && !have;i++)if(graph.cell[i].mover) {Lifts();break;}
        graph.elapsedMs=trap->Milliseconds()-began;
    }
    graphFrameTime=level.time;   // the last map's frame time is not this one's
    Identity();FreezeLoaded();Clearance();
    graphGeneration++;reachTopology++;StrafeNearChanged();guideTopo++;
    Report("map_load");
    GuidePreload();
    return graph.count>0?qtrue:qfalse;
}

// move_graph [rebuild|path x y z x y z]: report the prepared graph, rebuild
// it from the world as it stands, or search between two points. Bodies
// present at a rebuild refuse the cells they occupy, since the sample
// collides with them as the walk would; the report carries the client count.
void Svcmd_MoveGraph_f(void) {
    char arg[16];
    trap->Argv(1,arg,sizeof(arg));
    if(!Q_stricmp(arg,"rebuild")) {
        Build();
        if(!Graph_Save())trap->Print("[GRAPH] cache_write_failed\n");
        Identity();FreezeLoaded();Clearance();
        Report("rebuild");
    } else if(!Q_stricmp(arg,"path")) {
        static int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];
        vec3_t a,b;int i,from,to,count,cost=0,began=trap->Milliseconds(),used=0;char v[32];
        for(i=0;i<3;i++) {trap->Argv(2+i,v,sizeof(v));a[i]=atof(v);trap->Argv(5+i,v,sizeof(v));b[i]=atof(v);}
        from=Graph_Nearest(a,64,STEPSIZE*2);to=Graph_Nearest(b,64,STEPSIZE*2);
        count=Graph_Path(from,to,cells,edges,GRAPH_MAX_PATH,&cost);
        for(i=0;i<count;i++)if(edges[i]>=0)used++;
        trap->Print("[GRAPH] path from=%d to=%d cells=%d edges=%d cost_ms=%d elapsed_ms=%d\n",from,to,count,used,cost,trap->Milliseconds()-began);
        for(i=0;i<count;i++)if(edges[i]>=0)
            trap->Print("[GRAPH] path_edge at=%d index=%d type=%s\n",i,edges[i],EdgeTypeName(graph.edge[edges[i]].type));
    } else Report("report");
}
