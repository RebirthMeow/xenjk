/*
===========================================================================
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

/*
Headless replay of a SkirtClothLab "SKD1" trace through the C skirt solver
(codemp/rd-common/cloth/skirt_sim.c), the same way ClothResearch/tools/skirt_replay.js
replays it through the JS solver: same dt, same skinning matrices per frame, reset on
frame 0, blow-up -> reset. Writes the C positions of every processed frame to a binary
file that tools/cloth/compare_c_js.mjs compares against the JS solver.

  skirt_replay --dump <file.skd> --out <positions.bin> [--seconds N] [--set key=value ...]
               [--no-write] [--quiet]
  skirt_replay --asset <file.cloth> --pose <reel.skp> [--clips a,b,c] --out <positions.bin>
               [--stats <per-frame.csv>] [--set key=value ...] [--quiet]
  skirt_replay --asset <file.cloth> --mats <dump.cld> [--out <positions.bin>] [--stats <per-step.csv>] [--set ...]
      (an in-game r_clothDump, record for record)
  skirt_replay --asset <file.cloth> --mats <dump.cld> --frames [--fps F] [--hitch atMs:lenMs,...]
               [--hitch-every periodMs:lenMs] [--mode legacy|spread|carry] [--carry-frames X] [--max-steps N]
               [--hz 60] [--depth cm] [--window ms] [--stats <per-step.csv>] [--set key=value ...]
      (the game's per-frame stepping, rd-common/tr_cloth.cpp Cloth_Advance with rd-common/sim_frame.h, over the
       dump's poses as the motion: long frames and off-screen gaps, where more steps are owed than
       r_clothMaxSteps allows. See main_frames below.)
      (game asset SKA1 + pose reel SKP1, see docs/cloth/FORMAT.md;
       the clip table of the pose file resets the solver at each clip start)

Output file: "SKC1" u32 nVerts u32 nFrames, then per frame: u32 frameIndex, f32 pos[nVerts*3].

SKD1 layout (little endian): "SKD1" u32 version u32 jsonBytes json, refInv f32[nBones*16],
cloth {pos nrm tri maxDist animDrive skinBone skinW}, trimesh {pos tri skinBone skinW},
frames {u32 frame f32 elapsed f32 dt u32 valid f32 c2w[16] f32 cst[nBones*16] f32 sim[n*3]}.
*/
#include "../../codemp/rd-common/cloth/skirt_sim.h"
#include "../../codemp/rd-common/cloth/cloth_asset.h"
#include "../../codemp/rd-common/sim_frame.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define fseek64 _fseeki64
#define ftell64 _ftelli64
static double nowMs(void)
{
	LARGE_INTEGER f, c;
	QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
	return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}
#else
#include <time.h>
#include <sys/types.h>
#define fseek64 fseeko
#define ftell64 ftello
static double nowMs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return 1000.0 * (double)ts.tv_sec + 1e-6 * (double)ts.tv_nsec;
}
#endif

/* ---------------------------------------------------------------- tiny JSON reader */

typedef struct jv {
	int type;   /* 0 null, 1 bool, 2 number, 3 string, 4 array, 5 object */
	double num;
	char *str;
	struct jv **items;
	char **keys;
	int n;
} jv;

static const char *jp;

static void jskip(void) { while (*jp == ' ' || *jp == '\n' || *jp == '\r' || *jp == '\t') jp++; }

static jv *jnew(int type) { jv *v = (jv *)calloc(1, sizeof(jv)); v->type = type; return v; }

static jv *jparse(void);

static char *jstring(void)
{
	size_t cap = 16, n = 0;
	char *s = (char *)malloc(cap);
	jp++;   /* opening quote */
	while (*jp && *jp != '"') {
		char c = *jp++;
		if (c == '\\') {
			c = *jp++;
			switch (c) { case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break; case 'b': c = '\b'; break; case 'f': c = '\f'; break;
				case 'u': { unsigned u = 0; int k; for (k = 0; k < 4 && *jp; k++) { char h = *jp++; u = u * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10); } c = u < 128 ? (char)u : '?'; } break;
				default: break; }
		}
		if (n + 2 > cap) { cap *= 2; s = (char *)realloc(s, cap); }
		s[n++] = c;
	}
	if (*jp == '"') jp++;
	s[n] = 0;
	return s;
}

static jv *jparse(void)
{
	jv *v;
	jskip();
	if (*jp == '{') {
		v = jnew(5); jp++; jskip();
		while (*jp && *jp != '}') {
			char *k; jv *val;
			jskip(); k = jstring(); jskip(); if (*jp == ':') jp++;
			val = jparse();
			v->items = (jv **)realloc(v->items, sizeof(jv *) * (size_t)(v->n + 1));
			v->keys = (char **)realloc(v->keys, sizeof(char *) * (size_t)(v->n + 1));
			v->items[v->n] = val; v->keys[v->n] = k; v->n++;
			jskip(); if (*jp == ',') jp++;
			jskip();
		}
		if (*jp == '}') jp++;
		return v;
	}
	if (*jp == '[') {
		v = jnew(4); jp++; jskip();
		while (*jp && *jp != ']') {
			jv *val = jparse();
			v->items = (jv **)realloc(v->items, sizeof(jv *) * (size_t)(v->n + 1));
			v->items[v->n++] = val;
			jskip(); if (*jp == ',') jp++;
			jskip();
		}
		if (*jp == ']') jp++;
		return v;
	}
	if (*jp == '"') { v = jnew(3); v->str = jstring(); return v; }
	if (!strncmp(jp, "true", 4)) { v = jnew(1); v->num = 1; jp += 4; return v; }
	if (!strncmp(jp, "false", 5)) { v = jnew(1); jp += 5; return v; }
	if (!strncmp(jp, "null", 4)) { v = jnew(0); jp += 4; return v; }
	{
		char *end;
		v = jnew(2);
		v->num = strtod(jp, &end);
		if (end == jp) { jp++; }   /* malformed: skip a character so we always advance */
		else jp = end;
		return v;
	}
}

static jv *jget(const jv *o, const char *key)
{
	int i;
	if (!o || o->type != 5) return NULL;
	for (i = 0; i < o->n; i++) if (!strcmp(o->keys[i], key)) return o->items[i];
	return NULL;
}
static double jnum(const jv *v, double def) { return v && v->type == 2 ? v->num : def; }
/* first element of a [a, b] array, else def (JS: cfg.x ? cfg.x[0] : def) */
static double jnum0(const jv *v, double def) { return v && v->type == 4 && v->n > 0 ? jnum(v->items[0], def) : def; }

/* ---------------------------------------------------------------- SKD1 */

typedef struct {
	int nBones, nVerts, nTris, pelvisBone, refBone, nInfl;
	int nColVerts, nColTris;
	float hemZ;
	float *refInv;
	float *pos, *nrm; uint32_t *tri; float *maxDist, *animDrive; int32_t *skinB; float *skinW;
	float *colPos; uint32_t *colTri; int32_t *colSkinB; float *colSkinW;
	int nCaps; int32_t capA[16], capB[16]; double capR[16];
	skirt_config_t config;
	long long headerBytes, frameBytes;
	FILE *f;
	long long nFrames;
} skd_t;

static void *xread(FILE *f, size_t bytes)
{
	void *p = malloc(bytes ? bytes : 1);
	if (!p || (bytes && fread(p, 1, bytes, f) != bytes)) { fprintf(stderr, "short read (%zu bytes)\n", bytes); exit(2); }
	return p;
}

static int boneIndex(const jv *bones, const char *name)
{
	int i;
	if (!bones || bones->type != 4) return -1;
	for (i = 0; i < bones->n; i++) {
		const jv *b = bones->items[i];
		const char *s = b->type == 3 ? b->str : (jget(b, "name") && jget(b, "name")->type == 3 ? jget(b, "name")->str : NULL);
		if (s && !strcmp(s, name)) return i;
	}
	return -1;
}

static void openSkd(skd_t *h, const char *path)
{
	unsigned char head[12];
	uint32_t jsonBytes;
	char *json;
	jv *meta, *cfg, *tm, *bones, *hb;
	long long size;
	int NB, N, NT, i, side, c;
	static const struct { const char *from, *to; double r; } caps[6] = {
		{ "Forearm", "Hand", 2.8 }, { "Hand", "Finger1", 1.6 }, { "Hand", "Finger4", 1.6 },
		{ "Finger1", "Finger12", 1.0 }, { "Finger3", "Finger32", 1.0 }, { "Finger0", "Finger02", 1.0 } };
	memset(h, 0, sizeof(*h));
	h->f = fopen(path, "rb");
	if (!h->f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
	setvbuf(h->f, NULL, _IOFBF, 1 << 20);
	if (fread(head, 1, 12, h->f) != 12 || memcmp(head, "SKD1", 4)) { fprintf(stderr, "not an SKD1 dump\n"); exit(2); }
	memcpy(&jsonBytes, head + 8, 4);
	json = (char *)malloc((size_t)jsonBytes + 1);
	if (fread(json, 1, jsonBytes, h->f) != jsonBytes) { fprintf(stderr, "short json\n"); exit(2); }
	json[jsonBytes] = 0;
	jp = json; meta = jparse();
	NB = h->nBones = (int)jnum(jget(meta, "nBones"), 0);
	N = h->nVerts = (int)jnum(jget(meta, "nVerts"), 0);
	NT = h->nTris = (int)jnum(jget(meta, "nTris"), 0);
	h->pelvisBone = (int)jnum(jget(meta, "pelvisBone"), 0);
	h->hemZ = (float)jnum(jget(meta, "hemZ"), 0);
	h->nInfl = (int)jnum(jget(meta, "infl"), 12);
	cfg = jget(meta, "config");
	h->refBone = (int)jnum(jget(cfg, "referenceBone"), h->pelvisBone);   /* cfg.referenceBone ?? meta.pelvisBone ?? 0 */
	SkirtConfig_Defaults(&h->config);
	{
		const jv *g = jget(cfg, "gravity");
		if (g && g->type == 4 && g->n >= 3) for (i = 0; i < 3; i++) h->config.gravity[i] = jnum(g->items[i], 0);
	}
	h->config.density = jnum(jget(cfg, "density"), 0.35);
	h->config.minPerParticleMass = jnum(jget(cfg, "minPerParticleMass"), 1e-4);
	h->config.edge = jnum0(jget(cfg, "edge"), 1);
	h->config.bend = jnum0(jget(cfg, "bend"), 1);
	h->config.area = jnum0(jget(cfg, "area"), 1);
	h->config.tetherStiffness = jnum0(jget(cfg, "tetherStiffness"), 1);
	h->config.drag = jnum0(jget(cfg, "drag"), 0.035);
	h->config.lift = jnum0(jget(cfg, "lift"), 0.035);

	h->refInv = (float *)xread(h->f, sizeof(float) * (size_t)NB * 16);
	h->pos = (float *)xread(h->f, sizeof(float) * (size_t)N * 3);
	h->nrm = (float *)xread(h->f, sizeof(float) * (size_t)N * 3);
	h->tri = (uint32_t *)xread(h->f, sizeof(uint32_t) * (size_t)NT * 3);
	h->maxDist = (float *)xread(h->f, sizeof(float) * (size_t)N);
	h->animDrive = (float *)xread(h->f, sizeof(float) * (size_t)N);
	h->skinB = (int32_t *)xread(h->f, sizeof(int32_t) * (size_t)N * 12);
	h->skinW = (float *)xread(h->f, sizeof(float) * (size_t)N * 12);
	h->nInfl = 12;
	tm = jget(meta, "trimeshes");
	if (tm && tm->type == 4 && tm->n > 0) {
		const jv *t0 = tm->items[0];
		const int cv = (int)jnum(jget(t0, "verts"), 0), ct = (int)jnum(jget(t0, "tris"), 0);
		h->colPos = (float *)xread(h->f, sizeof(float) * (size_t)cv * 3);
		h->colTri = (uint32_t *)xread(h->f, sizeof(uint32_t) * (size_t)ct * 3);
		h->colSkinB = (int32_t *)xread(h->f, sizeof(int32_t) * (size_t)cv * 12);
		h->colSkinW = (float *)xread(h->f, sizeof(float) * (size_t)cv * 12);
		h->nColVerts = cv; h->nColTris = ct;
		for (i = 1; i < tm->n; i++) {   /* further trimeshes are in the file but unused by the solver */
			const jv *tt = tm->items[i];
			const int v2 = (int)jnum(jget(tt, "verts"), 0), t2 = (int)jnum(jget(tt, "tris"), 0);
			free(xread(h->f, sizeof(float) * (size_t)v2 * 3 + sizeof(uint32_t) * (size_t)t2 * 3 + sizeof(int32_t) * (size_t)v2 * 12 + sizeof(float) * (size_t)v2 * 12));
		}
	}
	h->headerBytes = ftell64(h->f);
	h->frameBytes = 16 + 64 + (long long)NB * 64 + (long long)N * 12;
	fseek64(h->f, 0, SEEK_END);
	size = ftell64(h->f);
	h->nFrames = (size - h->headerBytes) / h->frameBytes;
	fseek64(h->f, h->headerBytes, SEEK_SET);

	/* hand capsules from the Bip001 arm bones (JS handCapsules): names, or meta.handBones overrides */
	bones = jget(meta, "bones");
	hb = jget(meta, "handBones");
	h->nCaps = 0;
	for (side = 0; side < 2; side++) for (c = 0; c < 6; c++) {
		char nf[96], nt[96], kf[64], kt[64];
		const jv *of, *ot;
		int ia, ib;
		const char sc = side == 0 ? 'L' : 'R';
		snprintf(kf, sizeof kf, "%c-%s", sc, caps[c].from); snprintf(kt, sizeof kt, "%c-%s", sc, caps[c].to);
		snprintf(nf, sizeof nf, "Bip001-%c-%s", sc, caps[c].from); snprintf(nt, sizeof nt, "Bip001-%c-%s", sc, caps[c].to);
		of = jget(hb, kf); ot = jget(hb, kt);
		ia = boneIndex(bones, of && of->type == 3 ? of->str : nf);
		ib = boneIndex(bones, ot && ot->type == 3 ? ot->str : nt);
		if (ia >= 0 && ib >= 0) { h->capA[h->nCaps] = ia; h->capB[h->nCaps] = ib; h->capR[h->nCaps] = caps[c].r; h->nCaps++; }
	}
}

/* out[b] = refInv[b] * cst[b], both row-major 4x4 (row-vector); double maths, rounded to float like the JS Float32Array */
static void skinMatrices(const float *refInv, const float *cst, int nBones, float *out)
{
	int b, r, c;
	for (b = 0; b < nBones; b++) {
		const int o = b * 16;
		for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) {
			const double v = (double)refInv[o + r * 4] * (double)cst[o + c] + (double)refInv[o + r * 4 + 1] * (double)cst[o + 4 + c]
				+ (double)refInv[o + r * 4 + 2] * (double)cst[o + 8 + c] + (double)refInv[o + r * 4 + 3] * (double)cst[o + 12 + c];
			out[o + r * 4 + c] = (float)v;
		}
	}
}


/* ---------------------------------------------------------------- SKA1 asset + SKP1 pose reel */

typedef struct {
	unsigned char *buf; size_t size;
	double unitCm; int nBones, refBone, nVerts, nTris, nInfl, nColVerts, nColTris, nCaps, nRS, nRV;
	skirt_config_t config;
	const char *boneNames;   /* nBones * 32 */
	const float *refInv, *pos, *maxDist, *animDrive, *skinW, *colPos, *colSkinW;
	const uint32_t *tri, *colTri;
	const int32_t *skinB, *colSkinB, *capA, *capB;
	const double *capR;
	const char *rsName;      /* nRS * 36: name[32] + u32 count */
	const uint32_t *rmap; const float *rdist;   /* SKA1 only (unused by the replay) */
	const char *params; int paramsLen;           /* SKA2 solver parameters, "key value" lines */
	int version;
} ska_t;

/* the game's own reader (cloth_asset.c: SKA1 and SKA2, bounds and index checks, crc), so a format change
   is made once and the garments built as SKA2 (Vincent's cape and later) replay too */
static int loadSka(ska_t *a, const char *path)
{
	FILE *f = fopen(path, "rb");
	cloth_asset_t ca;
	char err[256];
	if (!f) { fprintf(stderr, "cannot open %s\n", path); return 0; }
	fseek(f, 0, SEEK_END); a->size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
	a->buf = (unsigned char *)malloc(a->size + 16);
	if (!a->buf || fread(a->buf, 1, a->size, f) != a->size) { fprintf(stderr, "short read %s\n", path); fclose(f); return 0; }
	fclose(f);
	if (!ClothAsset_Parse(a->buf, a->size, 1, &ca, err, sizeof err)) { fprintf(stderr, "%s: %s\n", path, err); return 0; }
	a->unitCm = ca.unitCm; a->nBones = ca.nBones; a->refBone = ca.refBone; a->nVerts = ca.nVerts; a->nTris = ca.nTris;
	a->nInfl = ca.nInfl; a->nColVerts = ca.nColVerts; a->nColTris = ca.nColTris; a->nCaps = ca.nCaps;
	a->nRS = ca.nRenderSurfaces; a->nRV = ca.nRenderVerts; a->config = ca.config;
	a->boneNames = ca.boneNames; a->refInv = ca.refInv; a->pos = ca.pos; a->maxDist = ca.maxDist; a->animDrive = ca.animDrive;
	a->skinW = ca.skinW; a->colPos = ca.colPos; a->colSkinW = ca.colSkinW; a->tri = ca.tri; a->colTri = ca.colTri;
	a->skinB = ca.skinBone; a->colSkinB = ca.colSkinBone; a->capA = ca.capA; a->capB = ca.capB; a->capR = ca.capR;
	a->rsName = ca.rsRecords; a->rmap = ca.renderSim; a->rdist = ca.renderDist;
	a->params = ca.params; a->paramsLen = ca.paramsLen; a->version = ca.version;
	return 1;
}

/* the asset's own parameters, applied before --set (the game applies them after its profile, before r_clothParam) */
static int applyAssetParams(skirt_sim_t *sim, const ska_t *a, int quiet)
{
	const char *p = a->params, *end = a->params ? a->params + a->paramsLen : NULL;
	int n = 0;
	while (p && p < end) {
		const char *eol = p;
		char line[128], k[64];
		size_t len;
		double v;
		while (eol < end && *eol != '\n') eol++;
		len = (size_t)(eol - p) < sizeof line - 1 ? (size_t)(eol - p) : sizeof line - 1;
		memcpy(line, p, len); line[len] = 0;
		if (sscanf(line, "%63s %lf", k, &v) == 2) {
			if (SkirtSim_SetParam(sim, k, v)) { if (!quiet) fprintf(stderr, "asset param %s: not a solver parameter, skipped\n", k); }
			else n++;
		}
		p = eol + 1;
	}
	return n;
}

static int main_reel(const char *assetPath, const char *posePath, const char *clipList, const char *outPath, const char *statsPath,
	int nSet, const char **setKey, const double *setVal, int quiet)
{
	ska_t ska;
	skirt_asset_t as;
	skirt_sim_t *sim;
	FILE *fp, *fo = NULL, *fs = NULL;
	unsigned char head[12];
	uint32_t jsonBytes, version;
	char *json;
	jv *meta, *clips;
	int NB, ci, k, i;
	long long frameBytes, dataStart, written = 0, totalSteps = 0, totalBlow = 0;
	float *mats, *outPos;
	unsigned char *fbuf;
	uint32_t u32;

	memset(&ska, 0, sizeof ska);
	if (!loadSka(&ska, assetPath)) return 2;
	memset(&as, 0, sizeof as);
	as.nBones = ska.nBones; as.refInv = ska.refInv; as.refBone = ska.refBone;
	as.nVerts = ska.nVerts; as.nTris = ska.nTris; as.pos = ska.pos; as.tri = ska.tri; as.maxDist = ska.maxDist; as.animDrive = ska.animDrive;
	as.nInfl = ska.nInfl; as.skinBone = ska.skinB; as.skinW = ska.skinW;
	as.nColVerts = ska.nColVerts; as.nColTris = ska.nColTris; as.colPos = ska.colPos; as.colTri = ska.colTri; as.colSkinBone = ska.colSkinB; as.colSkinW = ska.colSkinW;
	as.nCapsules = ska.nCaps; as.capBoneA = ska.capA; as.capBoneB = ska.capB; as.capRadius = ska.capR;
	as.config = ska.config;
	sim = SkirtSim_Create(&as, NULL);
	if (!sim) { fprintf(stderr, "SkirtSim_Create failed\n"); return 2; }
	applyAssetParams(sim, &ska, 0);
	for (i = 0; i < nSet; i++) if (SkirtSim_SetParam(sim, setKey[i], setVal[i])) { fprintf(stderr, "unknown param %s\n", setKey[i]); return 2; }

	fp = fopen(posePath, "rb");
	if (!fp) { fprintf(stderr, "cannot open %s\n", posePath); return 2; }
	setvbuf(fp, NULL, _IOFBF, 1 << 20);
	if (fread(head, 1, 12, fp) != 12 || memcmp(head, "SKP1", 4)) { fprintf(stderr, "not an SKP1 pose file\n"); return 2; }
	memcpy(&version, head + 4, 4); memcpy(&jsonBytes, head + 8, 4);
	json = (char *)malloc((size_t)jsonBytes + 1);
	if (fread(json, 1, jsonBytes, fp) != jsonBytes) { fprintf(stderr, "short json\n"); return 2; }
	json[jsonBytes] = 0; jp = json; meta = jparse();
	NB = (int)jnum(jget(meta, "nBones"), 0);
	if (NB != ska.nBones) { fprintf(stderr, "pose file has %d bones, asset %d\n", NB, ska.nBones); return 2; }
	clips = jget(meta, "clips");
	dataStart = ftell64(fp);
	frameBytes = 12 + (long long)NB * 64;
	fbuf = (unsigned char *)malloc((size_t)frameBytes);
	mats = (float *)malloc(sizeof(float) * (size_t)NB * 16);
	outPos = (float *)malloc(sizeof(float) * (size_t)ska.nVerts * 3);
	if (outPath) {
		fo = fopen(outPath, "wb");
		if (!fo) { fprintf(stderr, "cannot write %s\n", outPath); return 2; }
		setvbuf(fo, NULL, _IOFBF, 1 << 22);
		fwrite("SKC1", 1, 4, fo); u32 = (uint32_t)ska.nVerts; fwrite(&u32, 4, 1, fo); u32 = 0; fwrite(&u32, 4, 1, fo);
	}
	if (statsPath) { fs = fopen(statsPath, "w"); if (fs) fprintf(fs, "frame,clip,ms,contacts,hand,tethers,nan\n"); }
	if (!quiet) printf("asset %s: %d bones, %d verts, %d tris, collider %d verts / %d tris, %d capsules; skirt_real = %s, sim block %zu bytes\n",
		assetPath, ska.nBones, ska.nVerts, ska.nTris, ska.nColVerts, ska.nColTris, ska.nCaps, sizeof(skirt_real) == 8 ? "double" : "float", SkirtSim_RequiredSize(&as));

	for (ci = 0; clips && ci < clips->n; ci++) {
		const jv *c = clips->items[ci];
		const jv *nm = jget(c, "name");
		const char *name = nm && nm->type == 3 ? nm->str : "?";
		const long long start = (long long)jnum(jget(c, "start"), 0), nfr = (long long)jnum(jget(c, "frames"), 0);
		long long fi, blow = 0;
		double sum = 0, mx = 0, *times;
		int want = 1;
		if (clipList && *clipList) {
			char key[160];
			char *list = (char *)malloc(strlen(clipList) + 3);
			snprintf(key, sizeof key, ",%s,", name);
			sprintf(list, ",%s,", clipList);
			want = strstr(list, key) != NULL;
			free(list);
		}
		if (!want) continue;
		times = (double *)malloc(sizeof(double) * (size_t)(nfr ? nfr : 1));
		fseek64(fp, dataStart + start * frameBytes, SEEK_SET);
		for (fi = 0; fi < nfr; fi++) {
			uint32_t index; float dt; int bad = 0;
			double t0, ms;
			if (fread(fbuf, 1, (size_t)frameBytes, fp) != (size_t)frameBytes) { fprintf(stderr, "short frame\n"); return 2; }
			memcpy(&index, fbuf, 4); memcpy(&dt, fbuf + 4, 4);
			{ /* skinning matrices: refInv[b] * cst[b], double maths rounded to float (same as the SKD1 path) */
				const float *cst = (const float *)(fbuf + 12);
				int b, r, cc;
				for (b = 0; b < NB; b++) {
					const int o = b * 16;
					for (r = 0; r < 4; r++) for (cc = 0; cc < 4; cc++) {
						const double v = (double)ska.refInv[o + r * 4] * (double)cst[o + cc] + (double)ska.refInv[o + r * 4 + 1] * (double)cst[o + 4 + cc]
							+ (double)ska.refInv[o + r * 4 + 2] * (double)cst[o + 8 + cc] + (double)ska.refInv[o + r * 4 + 3] * (double)cst[o + 12 + cc];
						mats[o + r * 4 + cc] = (float)v;
					}
				}
			}
			t0 = nowMs();
			if (fi == 0) SkirtSim_Reset(sim, mats); else SkirtSim_Step(sim, (skirt_real)dt, mats);
			ms = nowMs() - t0;
			times[fi] = ms; sum += ms; if (ms > mx) mx = ms;
			SkirtSim_Read(sim, outPos);
			for (k = 0; k < ska.nVerts * 3; k++) if (!isfinite(outPos[k])) { bad = 1; break; }
			if (bad) { blow++; SkirtSim_Reset(sim, mats); SkirtSim_Read(sim, outPos); }
			if (fo) { fwrite(&index, 4, 1, fo); fwrite(outPos, sizeof(float), (size_t)ska.nVerts * 3, fo); written++; }
			if (fs) fprintf(fs, "%u,%s,%.4f,%d,%d,%d,%d\n", index, name, ms, SkirtSim_Stats(sim)->contacts, SkirtSim_Stats(sim)->handContacts, SkirtSim_Stats(sim)->tethersActive, bad);
		}
		{ /* median / p95 of the step times (insertion sort, clips are a few hundred frames) */
			long long a2, b2;
			for (a2 = 1; a2 < nfr; a2++) { double v = times[a2]; b2 = a2 - 1; while (b2 >= 0 && times[b2] > v) { times[b2 + 1] = times[b2]; b2--; } times[b2 + 1] = v; }
			printf("CLIP %s frames %lld mean_ms %.3f median_ms %.3f p95_ms %.3f max_ms %.3f blowups %lld\n", name, nfr,
				nfr ? sum / (double)nfr : 0.0, nfr ? times[nfr / 2] : 0.0, nfr ? times[(long long)((double)(nfr - 1) * 0.95)] : 0.0, mx, blow);
		}
		free(times);
		totalSteps += nfr; totalBlow += blow;
	}
	if (fo) {
		const uint32_t nw = (uint32_t)written;
		fflush(fo); fseek(fo, 8, SEEK_SET); fwrite(&nw, 4, 1, fo); fclose(fo);
	}
	if (fs) fclose(fs);
	printf("total frames %lld, blowups %lld\n", totalSteps, totalBlow);
	SkirtSim_Destroy(sim);
	return 0;
}

/* ---------------------------------------------------------------- CLD1 game dump (tr_cloth.cpp r_clothDump) */

/* Replays the exact in-game input: every record's skinning matrices (world cm) and dt; a reset record resets. Writes
SKC1 positions (frame index = record index) and a per-step csv with the solver diagnostics. */
/* the game settles a reset instance on its reset pose (r_clothSettle steps at r_clothHz, CPU path) before
 * the dump's reset record; --settle N [--settle-hz H] repeats that, or a dump from a game with r_clothSettle > 0 no
 * longer replays */
static int settleSteps = 0;
static double settleHz = 60.0;

static int main_mats(const char *assetPath, const char *matsPath, const char *outPath, const char *statsPath,
	int nSet, const char **setKey, const double *setVal, int quiet)
{
	ska_t ska;
	skirt_asset_t as;
	skirt_sim_t *sim;
	FILE *fp, *fo = NULL, *fs = NULL;
	int head[3], NB, N, i;
	long long rec = 0, written = 0, blow = 0;
	float *mats, *outPos, *gamePos;
	uint32_t u32;
	memset(&ska, 0, sizeof ska);
	if (!loadSka(&ska, assetPath)) return 2;
	memset(&as, 0, sizeof as);
	as.nBones = ska.nBones; as.refInv = ska.refInv; as.refBone = ska.refBone;
	as.nVerts = ska.nVerts; as.nTris = ska.nTris; as.pos = ska.pos; as.tri = ska.tri; as.maxDist = ska.maxDist; as.animDrive = ska.animDrive;
	as.nInfl = ska.nInfl; as.skinBone = ska.skinB; as.skinW = ska.skinW;
	as.nColVerts = ska.nColVerts; as.nColTris = ska.nColTris; as.colPos = ska.colPos; as.colTri = ska.colTri; as.colSkinBone = ska.colSkinB; as.colSkinW = ska.colSkinW;
	as.nCapsules = ska.nCaps; as.capBoneA = ska.capA; as.capBoneB = ska.capB; as.capRadius = ska.capR;
	as.config = ska.config;
	sim = SkirtSim_Create(&as, NULL);
	if (!sim) { fprintf(stderr, "SkirtSim_Create failed\n"); return 2; }
	applyAssetParams(sim, &ska, 0);
	for (i = 0; i < nSet; i++) if (SkirtSim_SetParam(sim, setKey[i], setVal[i])) { fprintf(stderr, "unknown param %s\n", setKey[i]); return 2; }
	fp = fopen(matsPath, "rb");
	if (!fp) { fprintf(stderr, "cannot open %s\n", matsPath); return 2; }
	setvbuf(fp, NULL, _IOFBF, 1 << 22);
	if (fread(head, 4, 3, fp) != 3 || head[0] != 0x31444c43) { fprintf(stderr, "not a CLD1 dump\n"); return 2; }
	NB = head[1]; N = head[2];
	if (NB != ska.nBones || N != ska.nVerts) { fprintf(stderr, "dump has %d bones / %d verts, asset %d / %d\n", NB, N, ska.nBones, ska.nVerts); return 2; }
	mats = (float *)malloc(sizeof(float) * (size_t)NB * 16);
	outPos = (float *)malloc(sizeof(float) * (size_t)N * 3);
	gamePos = (float *)malloc(sizeof(float) * (size_t)N * 3);
	if (outPath) {
		fo = fopen(outPath, "wb");
		if (!fo) { fprintf(stderr, "cannot write %s\n", outPath); return 2; }
		setvbuf(fo, NULL, _IOFBF, 1 << 22);
		fwrite("SKC1", 1, 4, fo); u32 = (uint32_t)N; fwrite(&u32, 4, 1, fo); u32 = 0; fwrite(&u32, 4, 1, fo);
	}
	if (statsPath) { fs = fopen(statsPath, "w"); if (fs) fprintf(fs, "rec,time,reset,dt,contacts,cone,coneDeg,yaw,feltAcc,feltAccCut,angVel,angVelUsed,game_maxdiff\n"); }
	for (;;) {
		int hi[2]; float hf[9];
		const skirt_stats_t *st;
		double md = 0;
		int k;
		if (fread(hi, 4, 2, fp) != 2) break;
		if (fread(hf, 4, 9, fp) != 9 || fread(mats, 4, (size_t)NB * 16, fp) != (size_t)NB * 16 || fread(gamePos, 4, (size_t)N * 3, fp) != (size_t)N * 3) break;
		if (hi[1] & 1) {
			SkirtSim_Reset(sim, mats);
			/* the game's r_clothSettle steps on the reset pose (tr_cloth.cpp Cloth_Reset, CPU path) */
			for (k = 0; k < settleSteps; k++) SkirtSim_Step(sim, (skirt_real)(1.0 / settleHz), mats);
		}
		else {
			SkirtSim_SetPoseDt(sim, Sim_DumpPoseDt(hi[1]));   /* a step that covered more pose time than its dt (sim_frame.h) */
			SkirtSim_Step(sim, (skirt_real)hf[0], mats);
		}
		SkirtSim_Read(sim, outPos);
		for (k = 0; k < N * 3; k++) {
			const double d = fabs((double)outPos[k] - (double)gamePos[k]);
			if (!isfinite(outPos[k])) { blow++; SkirtSim_Reset(sim, mats); SkirtSim_Read(sim, outPos); break; }
			if (d > md) md = d;
		}
		if (fo) { u32 = (uint32_t)rec; fwrite(&u32, 4, 1, fo); fwrite(outPos, sizeof(float), (size_t)N * 3, fo); written++; }
		st = SkirtSim_Stats(sim);
		if (fs) fprintf(fs, "%lld,%d,%d,%.6f,%d,%d,%.3f,%.2f,%.1f,%.1f,%.2f,%.2f,%.4f\n", rec, hi[0], hi[1] & 1, hf[0],
			(hi[1] & 1) ? 0 : st->contacts, (hi[1] & 1) ? 0 : st->coneCapped, st->coneDeg, st->yawRate, st->feltAcc, st->feltAccCut, st->angVel, st->angVelUsed, md);
		rec++;
	}
	if (fo) {
		const uint32_t nw = (uint32_t)written;
		fflush(fo); fseek(fo, 8, SEEK_SET); fwrite(&nw, 4, 1, fo); fclose(fo);
	}
	if (fs) fclose(fs);
	fclose(fp);
	if (!quiet) printf("records %lld, blowups %lld\n", rec, blow);
	SkirtSim_Destroy(sim);
	return 0;
}

/* ---------------------------------------------------------------- per-frame stepping over a game dump

The dump's records are the motion: world skinning matrices at the cloth rate (1/hz s apart). Render frames run at
--fps; a hitch is one frame of lenMs starting at atMs (a hitch, or a character the main view culled for that long:
the front end sees the same thing). Each frame's pose is the dump sampled at the frame time; steps are planned by
Sim_PlanSteps and driven by poses lerped between the stored previous pose and the frame's, as Cloth_Advance does.
--mode legacy is the earlier stepping (time dropped at the frame's end, the frame's pose stored as previous,
step poses lerped as world matrices); carry (default) is the fix (and SkirtPose_Lerp); spread is carry with nothing
carried.

Reported against a run of the same frames with no step limit: per step, the pose time it covered and how far the
collider moved in the pelvis frame; the frame velocity the solver measured against the real one; particles inside the
body after the step; the drawn state after each hitch.
Record: docs/cloth/records/2026-11-09-hair-cloth-views-and-dropped-steps.md. */

typedef struct {
	int frames, steps, capped, resets, teleports;
	double dropped, maxPoseMs, maxColMove, maxVelRatio, maxFeltAcc;
	long insideSum, insideSteps;
	int insideMax;
	double insideDepth;
} frStats_t;

static void fr_orthonormalise(float *m)
{
	float x[3], y[3], z[3], l, d;
	int k;
	l = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	for (k = 0; k < 3; k++) x[k] = l > 0 ? m[k] / l : 0;
	d = x[0] * m[4] + x[1] * m[5] + x[2] * m[6];
	for (k = 0; k < 3; k++) y[k] = m[4 + k] - d * x[k];
	l = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	for (k = 0; k < 3; k++) y[k] = l > 0 ? y[k] / l : 0;
	z[0] = x[1] * y[2] - x[2] * y[1]; z[1] = x[2] * y[0] - x[0] * y[2]; z[2] = x[0] * y[1] - x[1] * y[0];
	if (z[0] * m[8] + z[1] * m[9] + z[2] * m[10] < 0) for (k = 0; k < 3; k++) z[k] = -z[k];
	for (k = 0; k < 3; k++) { m[k] = x[k]; m[4 + k] = y[k]; m[8 + k] = z[k]; }
}

static void fr_lerp(float *o, const float *a, const float *b, double t, int NB)
{
	const float f = (float)t;
	int k, bn;
	for (k = 0; k < NB * 16; k++) o[k] = a[k] + f * (b[k] - a[k]);
	for (bn = 0; bn < NB; bn++) fr_orthonormalise(o + bn * 16);
}

/* the dump at time t (seconds from its first record); returns the record index it starts from. A record the game
reset at (a teleport in the demo) is not blended into: the pose holds until it. */
static int fr_poseAt(float *o, const float *all, const unsigned char *recReset, int nRec, int NB, double hz, double t)
{
	double f = t * hz;
	int i = (int)floor(f);
	if (i < 0) { i = 0; f = 0; }
	if (i >= nRec - 1) { i = nRec - 2; f = nRec - 1; }
	fr_lerp(o, all + (size_t)i * NB * 16, all + (size_t)(i + 1) * NB * 16, recReset[i + 1] ? 0.0 : f - i, NB);
	return i;
}

/* world points -> the pelvis frame of a pose (as Cloth_StoreRel) */
static void fr_toFrame(const float *mats, int ref, const float *w, float *rel, int n)
{
	float F[16];
	int i;
	memcpy(F, mats + ref * 16, sizeof F);
	fr_orthonormalise(F);
	for (i = 0; i < n; i++, w += 3, rel += 3) {
		const float x = w[0] - F[12], y = w[1] - F[13], z = w[2] - F[14];
		rel[0] = x * F[0] + y * F[1] + z * F[2];
		rel[1] = x * F[4] + y * F[5] + z * F[6];
		rel[2] = x * F[8] + y * F[9] + z * F[10];
	}
}

/* every 'stride'-th collider vertex skinned by a pose, in that pose's pelvis frame */
static void fr_collider(const ska_t *a, const float *mats, int stride, float *out)
{
	const int NI = a->nInfl;
	int v, k, n = 0;
	for (v = 0; v < a->nColVerts; v += stride, n++) {
		const float *p = a->colPos + v * 3;
		float x = 0, y = 0, z = 0;
		for (k = 0; k < NI; k++) {
			const int b = a->colSkinB[v * NI + k];
			const float w = a->colSkinW[v * NI + k];
			const float *m;
			if (b < 0 || w == 0.0f) continue;
			m = mats + b * 16;
			x += w * (p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12]);
			y += w * (p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13]);
			z += w * (p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14]);
		}
		out[n * 3] = x; out[n * 3 + 1] = y; out[n * 3 + 2] = z;
	}
	fr_toFrame(mats, a->refBone, out, out, n);
}

/* --draw-lead -1 (default: the newest step state, as before), 0 or 1: the drawn state as rd-common/tr_cloth.cpp
   Cloth_BuildDraw makes it from the last two step states A, B (pelvis frame of each): A + t (B - A), t = acc * hz in
   [0, 1], plus 1 under r_clothDrawLead 1 */
static int fr_drawLead = -1;

static void fr_run(const ska_t *ska, skirt_sim_t *sim, const float *all, const unsigned char *recReset, int nRec, double hz,
	const double *ft, int nFrames, int maxSteps, int mode, int worldLerp, double carryFrames, double depth, int insideEvery,
	float *drawn, frStats_t *st, FILE *fs, const char *tag)
{
	const int NB = ska->nBones, N = ska->nVerts, ref = ska->refBone, stride = 4;
	const int nCol = (ska->nColVerts + stride - 1) / stride;
	const double step = 1.0 / hz;
	float *prev = (float *)malloc(sizeof(float) * NB * 16), *cur = (float *)malloc(sizeof(float) * NB * 16);
	float *stp = (float *)malloc(sizeof(float) * NB * 16), *out = (float *)malloc(sizeof(float) * N * 3);
	float *colA = (float *)malloc(sizeof(float) * (nCol + 1) * 3), *colB = (float *)malloc(sizeof(float) * (nCol + 1) * 3);
	float *simPose = (float *)malloc(sizeof(float) * NB * 16);   /* the pose of the solver's last step or reset */
	float *relA = (float *)malloc(sizeof(float) * N * 3), *relB = (float *)malloc(sizeof(float) * N * 3);   /* fr_drawLead */
	float *relZ = (float *)malloc(sizeof(float) * N * 3);   /* the state before relA (draw lead 2) */
	double acc = 0, baseLag = 0, lastT = ft[0], prevFrame = 0, lastPoseT = ft[0];
	int f, k, lastRec, stepNo = 0;

	memset(st, 0, sizeof *st);
	lastRec = fr_poseAt(cur, all, recReset, nRec, NB, hz, ft[0]);
	SkirtSim_Reset(sim, cur);
	SkirtSim_Read(sim, out);
	fr_toFrame(cur, ref, out, drawn, N);
	memcpy(relA, drawn, sizeof(float) * N * 3);
	memcpy(relB, drawn, sizeof(float) * N * 3);
	memcpy(relZ, drawn, sizeof(float) * N * 3);
	fr_collider(ska, cur, stride, colA);
	memcpy(prev, cur, sizeof(float) * NB * 16);
	memcpy(simPose, cur, sizeof(float) * NB * 16);

	for (f = 1; f < nFrames; f++) {
		simStepPlan_t plan;
		const double frame = ft[f] - lastT, baseT = lastT - baseLag;
		const int rec = fr_poseAt(cur, all, recReset, nRec, NB, hz, ft[f]);
		int r, crossed = 0;
		for (r = lastRec + 1; r <= rec && r < nRec; r++) crossed |= recReset[r];
		lastRec = rec;
		st->frames++;
		if (crossed) {
			/* the demo teleported here: the glue's teleport test resets */
			SkirtSim_Reset(sim, cur);
			acc = 0; baseLag = 0; lastT = ft[f]; lastPoseT = ft[f]; prevFrame = frame;
			memcpy(prev, cur, sizeof(float) * NB * 16);
			memcpy(simPose, cur, sizeof(float) * NB * 16);
			fr_collider(ska, cur, stride, colA);
			st->resets++;
			SkirtSim_Read(sim, out);
			fr_toFrame(cur, ref, out, drawn + (size_t)f * N * 3, N);
			memcpy(relA, drawn + (size_t)f * N * 3, sizeof(float) * N * 3);
			memcpy(relB, relA, sizeof(float) * N * 3);
			memcpy(relZ, relA, sizeof(float) * N * 3);
			continue;
		}
		Sim_PlanSteps(&plan, acc, baseLag, frame, step, maxSteps, mode, Sim_Carry(carryFrames, maxSteps, step, frame, prevFrame));
		prevFrame = frame;
		st->capped += plan.capped;
		st->dropped += plan.dropped;
		for (k = 0; k < plan.n; k++) {
			const double alpha = Sim_StepAlpha(&plan, k);
			const double poseT = baseT + alpha * plan.span, poseMs = (poseT - lastPoseT) * 1000;
			const double told = plan.stretched ? plan.poseDt : step;
			const float *mats = cur;
			const skirt_stats_t *ss;
			double move = 0, ratio, md = 0;
			int i, inside = -1;
			if (alpha < 0.999) {
				if (worldLerp) fr_lerp(stp, prev, cur, alpha, NB);
				else SkirtPose_Lerp(stp, prev, cur, (float)alpha, NB, ref);
				mats = stp;
			}
			SkirtSim_SetPoseDt(sim, Sim_StepPoseDt(&plan));
			SkirtSim_Step(sim, (skirt_real)step, mats);
			ss = SkirtSim_Stats(sim);
			memcpy(simPose, mats, sizeof(float) * NB * 16);
			if (fr_drawLead >= 0) {   /* the step states the draw blends: B the newest, A the one before */
				float *t = relZ; relZ = relA; relA = relB; relB = t;
				SkirtSim_Read(sim, out);
				fr_toFrame(mats, ref, out, relB, N);
			}
			if (ss->nan) { SkirtSim_Reset(sim, cur); memcpy(simPose, cur, sizeof(float) * NB * 16); st->resets++; }
			fr_collider(ska, mats, stride, colB);
			for (i = 0; i < nCol * 3; i += 3) {
				const double dx = colB[i] - colA[i], dy = colB[i + 1] - colA[i + 1], dz = colB[i + 2] - colA[i + 2];
				const double d = sqrt(dx * dx + dy * dy + dz * dz);
				if (d > move) move = d;
			}
			{ float *t = colA; colA = colB; colB = t; }
			ratio = poseMs * 0.001 / told;
			if (insideEvery > 0 && stepNo % insideEvery == 0) {
				inside = SkirtSim_CountInside(sim, depth, 1, &md);
				st->insideSum += inside;
				st->insideSteps++;
				if (inside > st->insideMax) st->insideMax = inside;
				if (md > st->insideDepth) st->insideDepth = md;
			}
			stepNo++;
			st->steps++;
			if (poseMs > st->maxPoseMs) st->maxPoseMs = poseMs;
			if (move > st->maxColMove) st->maxColMove = move;
			if (ratio > st->maxVelRatio) st->maxVelRatio = ratio;
			if (ss->feltAcc > st->maxFeltAcc) st->maxFeltAcc = ss->feltAcc;
			st->teleports += ss->teleport;
			lastPoseT = poseT;
			if (fs) fprintf(fs, "%s,%d,%.3f,%d,%d,%.5f,%.3f,%.3f,%.3f,%.1f,%.1f,%d,%.3f,%d\n", tag, f, ft[f] * 1000, k, plan.capped + 2 * plan.stretched,
				alpha, poseMs, ratio, move, ss->feltAcc, ss->feltAccCut, inside, md, ss->contacts);
		}
		acc = plan.accAfter;
		baseLag = plan.baseLag;
		lastT = ft[f];
		if (plan.baseLag > 0) {   /* the solver is behind: keep the pose of its last step */
			if (worldLerp) fr_lerp(stp, prev, cur, plan.baseAlpha, NB);
			else SkirtPose_Lerp(stp, prev, cur, (float)plan.baseAlpha, NB, ref);
			memcpy(prev, stp, sizeof(float) * NB * 16);
		} else {
			memcpy(prev, cur, sizeof(float) * NB * 16);
		}
		/* the newest step state in the pelvis frame of the pose it was solved at (what the draw places) */
		SkirtSim_Read(sim, out);
		fr_toFrame(simPose, ref, out, drawn + (size_t)f * N * 3, N);
		if (fr_drawLead >= 0) {
			float t = (float)(acc * hz), *d = drawn + (size_t)f * N * 3;
			int i;
			if (t < 0) t = 0;
			if (t > 1) t = 1;
			if (fr_drawLead == 2) {
				/* the old pair's extrapolation blended into the new pair's over one step, s the fraction of
				   a step since the newest state: (1-s) (B + (1+s)(B-A)) + s (C + s(C-B)), A B C oldest to newest. s = 0 gives
				   2B - A (where lead 1 was heading), s = 1 gives 2C - B (where the next step's formula starts) */
				const float sx = t < 1 ? t : 1;
				for (i = 0; i < N * 3; i++) {
					const float a = relZ[i], b = relA[i], c = relB[i];
					d[i] = (1 - sx) * (b + (1 + sx) * (b - a)) + sx * (c + sx * (c - b));
				}
			} else {
				if (fr_drawLead) t += 1.0f;
				for (i = 0; i < N * 3; i++) d[i] = relA[i] + t * (relB[i] - relA[i]);
			}
		}
	}
	free(prev); free(cur); free(stp); free(out); free(colA); free(colB); free(simPose); free(relA); free(relB); free(relZ);
}

static int fr_pairs(const char *s, double *at, double *len, int max)
{
	int n = 0;
	while (*s && n < max) {
		if (sscanf(s, "%lf:%lf", &at[n], &len[n]) != 2) break;
		n++;
		s = strchr(s, ',');
		if (!s) break;
		s++;
	}
	return n;
}

typedef struct {
	double fps, hz, depth, windowMs, carryFrames, everyPeriod, everyLen;
	double hitchAt[64], hitchLen[64];
	int nHitch, maxSteps, mode, insideEvery;
	const char *modeName;
	double drift[3];		/* --drift: a steady velocity added to the recorded motion (asset units/s) */
} frArgs_t;

static int main_frames(const char *assetPath, const char *matsPath, const char *statsPath, const frArgs_t *A,
	int nSet, const char **setKey, const double *setVal)
{
	ska_t ska;
	skirt_asset_t as;
	skirt_sim_t *sim;
	FILE *fp, *fs = NULL;
	int head[3], NB, N, i, nRec = 0, cap = 0, nFrames = 0, nHitch = A->nHitch, f, pass;
	float *all = NULL, *skip, *drawRef, *drawRun;
	unsigned char *recReset = NULL, *isHitch;
	double hitchAt[64], hitchLen[64], total, *ft;
	frStats_t stRef, stRun;
	memset(&ska, 0, sizeof ska);
	if (!loadSka(&ska, assetPath)) return 2;
	memset(&as, 0, sizeof as);
	as.nBones = ska.nBones; as.refInv = ska.refInv; as.refBone = ska.refBone;
	as.nVerts = ska.nVerts; as.nTris = ska.nTris; as.pos = ska.pos; as.tri = ska.tri; as.maxDist = ska.maxDist; as.animDrive = ska.animDrive;
	as.nInfl = ska.nInfl; as.skinBone = ska.skinB; as.skinW = ska.skinW;
	as.nColVerts = ska.nColVerts; as.nColTris = ska.nColTris; as.colPos = ska.colPos; as.colTri = ska.colTri; as.colSkinBone = ska.colSkinB; as.colSkinW = ska.colSkinW;
	as.nCapsules = ska.nCaps; as.capBoneA = ska.capA; as.capBoneB = ska.capB; as.capRadius = ska.capR;
	as.config = ska.config;
	fp = fopen(matsPath, "rb");
	if (!fp) { fprintf(stderr, "cannot open %s\n", matsPath); return 2; }
	setvbuf(fp, NULL, _IOFBF, 1 << 22);
	if (fread(head, 4, 3, fp) != 3 || head[0] != 0x31444c43) { fprintf(stderr, "not a CLD1 dump\n"); return 2; }
	NB = head[1]; N = head[2];
	if (NB != ska.nBones || N != ska.nVerts) { fprintf(stderr, "dump has %d bones / %d verts, asset %d / %d\n", NB, N, ska.nBones, ska.nVerts); return 2; }
	skip = (float *)malloc(sizeof(float) * (size_t)N * 3);
	for (;;) {
		int hi[2]; float hf[9];
		if (fread(hi, 4, 2, fp) != 2 || fread(hf, 4, 9, fp) != 9) break;
		if (nRec == cap) {
			cap = cap ? cap * 2 : 1024;
			all = (float *)realloc(all, sizeof(float) * (size_t)cap * NB * 16);
			recReset = (unsigned char *)realloc(recReset, (size_t)cap);
		}
		if (fread(all + (size_t)nRec * NB * 16, 4, (size_t)NB * 16, fp) != (size_t)NB * 16 || fread(skip, 4, (size_t)N * 3, fp) != (size_t)N * 3) break;
		recReset[nRec] = (unsigned char)((hi[1] & 1) && nRec > 0);
		nRec++;
	}
	fclose(fp);
	free(skip);
	if (nRec < 3) { fprintf(stderr, "dump too short\n"); return 2; }
	/* --drift vx,vy,vz (asset units/s): a steady velocity added to the whole recorded motion (every bone's origin moves
	   by drift * t), to run the dump at bike or CPM speeds */
	if (A->drift[0] != 0 || A->drift[1] != 0 || A->drift[2] != 0)
		for (i = 0; i < nRec; i++) {
			const double t = i / A->hz;
			int b, k;
			for (b = 0; b < NB; b++)
				for (k = 0; k < 3; k++) all[((size_t)i * NB + b) * 16 + 12 + k] += (float)(A->drift[k] * t);
		}

	memcpy(hitchAt, A->hitchAt, sizeof hitchAt); memcpy(hitchLen, A->hitchLen, sizeof hitchLen);
	total = (nRec - 1) / A->hz;
	if (A->everyPeriod > 0) {
		double t;
		for (t = A->everyPeriod; nHitch < 64 && t + A->everyLen < total * 1000; t += A->everyPeriod) { hitchAt[nHitch] = t; hitchLen[nHitch++] = A->everyLen; }
	}
	for (i = 1; i < nHitch; i++) {   /* the schedule takes hitches in list order: --hitch and --hitch-every merged by start */
		int j;
		for (j = i; j > 0 && hitchAt[j - 1] > hitchAt[j]; j--) {
			const double ta = hitchAt[j], tl = hitchLen[j];
			hitchAt[j] = hitchAt[j - 1]; hitchLen[j] = hitchLen[j - 1];
			hitchAt[j - 1] = ta; hitchLen[j - 1] = tl;
		}
	}
	{
		const int fcap = (int)(total * A->fps) + nHitch + 8;
		double t = 0;
		int h = 0;
		ft = (double *)malloc(sizeof(double) * (size_t)fcap);
		isHitch = (unsigned char *)calloc((size_t)fcap, 1);
		while (t <= total && nFrames < fcap) {
			ft[nFrames++] = t;
			if (h < nHitch && t * 1000 >= hitchAt[h]) {
				t += hitchLen[h++] * 0.001;
				if (t <= total && nFrames < fcap) isHitch[nFrames] = 1;
			} else t += 1.0 / A->fps;
		}
	}
	drawRef = (float *)calloc((size_t)nFrames * N * 3 + 1, sizeof(float));
	drawRun = (float *)calloc((size_t)nFrames * N * 3 + 1, sizeof(float));
	if (!drawRef || !drawRun) { fprintf(stderr, "out of memory for %d frames\n", nFrames); return 2; }
	if (statsPath) { fs = fopen(statsPath, "w"); if (fs) fprintf(fs, "run,frame,ms,step,capped,alpha,poseMs,velRatio,colliderMove,feltAcc,feltAccCut,inside,insideDepth,contacts\n"); }
	for (pass = 0; pass < 2; pass++) {
		sim = SkirtSim_Create(&as, NULL);
		if (!sim) { fprintf(stderr, "SkirtSim_Create failed\n"); return 2; }
		applyAssetParams(sim, &ska, 0);
		for (i = 0; i < nSet; i++) if (SkirtSim_SetParam(sim, setKey[i], setVal[i])) { fprintf(stderr, "unknown param %s\n", setKey[i]); return 2; }
		if (pass == 0) fr_run(&ska, sim, all, recReset, nRec, A->hz, ft, nFrames, SIM_PLAN_MAX_STEPS, SIM_STEPS_LEGACY, 0, 0, A->depth, A->insideEvery, drawRef, &stRef, fs, "nolimit");
		else fr_run(&ska, sim, all, recReset, nRec, A->hz, ft, nFrames, A->maxSteps, A->mode, A->mode == SIM_STEPS_LEGACY, A->carryFrames, A->depth, A->insideEvery, drawRun, &stRun, fs, A->modeName);
		SkirtSim_Destroy(sim);
		if (pass == 0 && stRef.dropped > 0) {   /* a gap over SIM_PLAN_MAX_STEPS steps: the reference lost time itself */
			fprintf(stderr, "the nolimit reference dropped %.0f ms (a frame owed more than %d steps): it is no reference; use shorter hitches\n",
				stRef.dropped * 1000, SIM_PLAN_MAX_STEPS);
			if (fs) fclose(fs);
			return 2;
		}
	}
	if (fs) fclose(fs);
	{
		/* drawn state against the unlimited run after each hitch: dynamic particles only */
		double mx = 0, sum = 0, mxLate = 0, sumLate = 0;
		long cnt = 0, cntLate = 0;
		const double lateMs = 100;
		for (f = 0; f < nFrames; f++) {
			int g;
			if (!isHitch[f]) continue;
			for (g = f; g < nFrames && (ft[g] - ft[f]) * 1000 <= A->windowMs; g++) {
				const float *x = drawRun + (size_t)g * N * 3, *y = drawRef + (size_t)g * N * 3;
				const int late = (ft[g] - ft[f]) * 1000 >= lateMs;
				for (i = 0; i < N; i++) {
					double d;
					if (ska.maxDist[i] <= 0) continue;   /* pinned */
					d = sqrt(((double)x[i * 3] - y[i * 3]) * ((double)x[i * 3] - y[i * 3]) + ((double)x[i * 3 + 1] - y[i * 3 + 1]) * ((double)x[i * 3 + 1] - y[i * 3 + 1])
						+ ((double)x[i * 3 + 2] - y[i * 3 + 2]) * ((double)x[i * 3 + 2] - y[i * 3 + 2]));
					if (d > mx) mx = d;
					sum += d * d; cnt++;
					if (late) { if (d > mxLate) mxLate = d; sumLate += d * d; cntLate++; }
				}
			}
		}
		printf("dump %d records (%.2f s), %d particles, collider %d verts / %d tris; %.0f fps, %d hitches, max steps %d, %.0f Hz\n",
			nRec, total, N, ska.nColVerts, ska.nColTris, A->fps, nHitch, A->maxSteps, A->hz);
		for (pass = 0; pass < 2; pass++) {
			const frStats_t *s = pass ? &stRun : &stRef;
			printf("%-8s teleport steps %d of %d (felt motion zeroed by the solver's maxFrameVel / maxFrameAngVel)\n",
				pass ? A->modeName : "nolimit", s->teleports, s->steps);
			printf("%-8s frames %d steps %d capped %d dropped %.0f ms resets %d | per step: pose %.1f ms, collider move %.2f cm | "
				"solver frame velocity x%.2f of the real one, felt acc %.0f cm/s2 | inside the body by over %.1f cm: mean %.2f max %d particles, deepest %.2f cm (%ld steps sampled)\n",
				pass ? A->modeName : "nolimit", s->frames, s->steps, s->capped, s->dropped * 1000, s->resets, s->maxPoseMs, s->maxColMove, s->maxVelRatio,
				s->maxFeltAcc, A->depth, s->insideSteps ? (double)s->insideSum / s->insideSteps : 0.0, s->insideMax, s->insideDepth, s->insideSteps);
		}
		printf("%-8s drawn state against nolimit, %.0f ms after each hitch: max %.2f cm, rms %.2f cm; from %.0f ms after: max %.2f cm, rms %.2f cm\n",
			A->modeName, A->windowMs, mx, cnt ? sqrt(sum / cnt) : 0.0, lateMs, mxLate, cntLate ? sqrt(sumLate / cntLate) : 0.0);
		if (fr_drawLead >= 0) {
			/* per-frame jerk of the drawn state (pelvis frame), |x(f+1) - 2 x(f) + x(f-1)| over the dynamic
			   particles, frames of equal length only (a hitch is not counted); a saw-tooth at the step boundaries shows
			   in the high percentiles */
			int pass2;
			for (pass2 = 0; pass2 < 2; pass2++) {
				const float *D = pass2 ? drawRun : drawRef;
				double s2 = 0, worst = 0, *perFrame = (double *)calloc((size_t)nFrames, sizeof(double)), p99;
				long c2 = 0;
				int nf = 0, g;
				for (f = 1; f + 1 < nFrames; f++) {
					double fm = 0;
					if (isHitch[f] || isHitch[f + 1] || fabs((ft[f + 1] - ft[f]) - (ft[f] - ft[f - 1])) > 1e-6) continue;
					for (i = 0; i < N; i++) {
						double j2 = 0;
						int k;
						if (ska.maxDist[i] <= 0) continue;
						for (k = 0; k < 3; k++) {
							const double j = (double)D[((size_t)(f + 1) * N + i) * 3 + k] - 2.0 * D[((size_t)f * N + i) * 3 + k] + D[((size_t)(f - 1) * N + i) * 3 + k];
							j2 += j * j;
						}
						s2 += j2; c2++;
						if (j2 > fm) fm = j2;
					}
					perFrame[nf++] = sqrt(fm);
					if (sqrt(fm) > worst) worst = sqrt(fm);
				}
				/* the 99th percentile of the frames' worst particle */
				for (g = 0; g < nf; g++) { int h; for (h = g + 1; h < nf; h++) if (perFrame[h] < perFrame[g]) { double t = perFrame[g]; perFrame[g] = perFrame[h]; perFrame[h] = t; } }
				p99 = nf ? perFrame[(int)(0.99 * (nf - 1))] : 0;
				printf("%-8s draw lead %d: per-frame jerk of the drawn state, rms %.3f cm, worst particle per frame p50 %.3f p99 %.3f max %.3f cm (%d frames)\n",
					pass2 ? A->modeName : "nolimit", fr_drawLead, c2 ? sqrt(s2 / c2) : 0.0, nf ? perFrame[nf / 2] : 0.0, p99, worst, nf);
				free(perFrame);
			}
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *dumpPath = NULL, *outPath = NULL;
	double seconds = 1e30;
	int quiet = 0, noWrite = 0, i;
	const char *setKey[64]; double setVal[64]; int nSet = 0;
	const char *assetPath = NULL, *posePath = NULL, *clipList = NULL, *statsPath = NULL, *matsPath = NULL;
	int framesMode = 0;
	frArgs_t FA;
	skd_t h;
	skirt_asset_t as;
	skirt_sim_t *sim;
	unsigned char *fbuf;
	float *mats, *outPos;
	long long nFrames, fi;
	double stepMs = 0, tStart, tReadMs = 0;
	long long steps = 0, blowups = 0, written = 0;
	FILE *fo = NULL;
	uint32_t u32;

	memset(&FA, 0, sizeof FA);
	FA.fps = 144; FA.hz = 60; FA.depth = 0.5; FA.windowMs = 1000; FA.carryFrames = 4; FA.maxSteps = 4; FA.mode = SIM_STEPS_CARRY;
	FA.modeName = "carry"; FA.insideEvery = 1;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--frames")) framesMode = 1;
		else if (!strcmp(argv[i], "--fps") && i + 1 < argc) FA.fps = atof(argv[++i]);
		else if (!strcmp(argv[i], "--hz") && i + 1 < argc) FA.hz = atof(argv[++i]);
		else if (!strcmp(argv[i], "--draw-lead") && i + 1 < argc) fr_drawLead = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--drift") && i + 1 < argc) sscanf(argv[++i], "%lf,%lf,%lf", &FA.drift[0], &FA.drift[1], &FA.drift[2]);
		else if (!strcmp(argv[i], "--depth") && i + 1 < argc) FA.depth = atof(argv[++i]);
		else if (!strcmp(argv[i], "--window") && i + 1 < argc) FA.windowMs = atof(argv[++i]);
		else if (!strcmp(argv[i], "--carry-frames") && i + 1 < argc) FA.carryFrames = atof(argv[++i]);
		else if (!strcmp(argv[i], "--max-steps") && i + 1 < argc) FA.maxSteps = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--inside-every") && i + 1 < argc) FA.insideEvery = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--hitch") && i + 1 < argc) FA.nHitch = fr_pairs(argv[++i], FA.hitchAt, FA.hitchLen, 64);
		else if (!strcmp(argv[i], "--hitch-every") && i + 1 < argc) sscanf(argv[++i], "%lf:%lf", &FA.everyPeriod, &FA.everyLen);
		else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
			FA.modeName = argv[++i];
			if (!strcmp(FA.modeName, "legacy")) FA.mode = SIM_STEPS_LEGACY;
			else if (!strcmp(FA.modeName, "spread")) FA.carryFrames = 0;
			else if (strcmp(FA.modeName, "carry")) { fputs("--mode legacy|spread|carry", stderr); return 2; }
		}
		else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dumpPath = argv[++i];
		else if (!strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
		else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
		else if (!strcmp(argv[i], "--quiet")) quiet = 1;
		else if (!strcmp(argv[i], "--asset") && i + 1 < argc) assetPath = argv[++i];
		else if (!strcmp(argv[i], "--pose") && i + 1 < argc) posePath = argv[++i];
		else if (!strcmp(argv[i], "--mats") && i + 1 < argc) matsPath = argv[++i];
		else if (!strcmp(argv[i], "--clips") && i + 1 < argc) clipList = argv[++i];
		else if (!strcmp(argv[i], "--stats") && i + 1 < argc) statsPath = argv[++i];
		else if (!strcmp(argv[i], "--no-write")) noWrite = 1;
		else if (!strcmp(argv[i], "--settle") && i + 1 < argc) settleSteps = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--settle-hz") && i + 1 < argc) settleHz = atof(argv[++i]);
		else if (!strcmp(argv[i], "--set") && i + 1 < argc && nSet < 64) {
			char *eq = strchr(argv[++i], '=');
			if (!eq) { fprintf(stderr, "--set key=value\n"); return 2; }
			*eq = 0; setKey[nSet] = argv[i]; setVal[nSet] = atof(eq + 1); nSet++;
		} else { fprintf(stderr, "usage: skirt_replay --dump f.skd --out pos.bin [--seconds N] [--set k=v] [--no-write] [--quiet]\n       skirt_replay --asset f.cloth --pose reel.skp [--clips a,b] --out pos.bin [--stats s.csv]\n       skirt_replay --asset f.cloth --mats dump.cld [--out pos.bin] [--stats s.csv]\n       skirt_replay --asset f.cloth --mats dump.cld --frames [--fps F] [--hitch at:len,...] [--hitch-every period:len] [--mode legacy|spread|carry] ...\n(see the comment at the top of skirt_replay.c)\n"); return 2; }
	}
	if (matsPath) {
		if (!assetPath) { fprintf(stderr, "--mats needs --asset\n"); return 2; }
		if (framesMode) return main_frames(assetPath, matsPath, statsPath, &FA, nSet, setKey, setVal);
		return main_mats(assetPath, matsPath, outPath, statsPath, nSet, setKey, setVal, quiet);
	}
	if (assetPath || posePath) {
		if (!assetPath || !posePath) { fprintf(stderr, "--asset and --pose go together\n"); return 2; }
		return main_reel(assetPath, posePath, clipList, outPath, statsPath, nSet, setKey, setVal, quiet);
	}
	if (!outPath) noWrite = 1;

	if (!dumpPath) { fprintf(stderr, "--dump <file.skd> is required (the trace is not in this repository)\n"); return 2; }
	openSkd(&h, dumpPath);
	memset(&as, 0, sizeof as);
	as.nBones = h.nBones; as.refInv = h.refInv; as.refBone = h.refBone;
	as.nVerts = h.nVerts; as.nTris = h.nTris; as.pos = h.pos; as.tri = h.tri; as.maxDist = h.maxDist; as.animDrive = h.animDrive;
	as.nInfl = h.nInfl; as.skinBone = h.skinB; as.skinW = h.skinW;
	as.nColVerts = h.nColVerts; as.nColTris = h.nColTris; as.colPos = h.colPos; as.colTri = h.colTri; as.colSkinBone = h.colSkinB; as.colSkinW = h.colSkinW;
	as.nCapsules = h.nCaps; as.capBoneA = h.capA; as.capBoneB = h.capB; as.capRadius = h.capR;
	as.config = h.config;

	sim = SkirtSim_Create(&as, NULL);
	if (!sim) { fprintf(stderr, "SkirtSim_Create failed\n"); return 2; }
	for (i = 0; i < nSet; i++) {
		if (SkirtSim_SetParam(sim, setKey[i], setVal[i])) { fprintf(stderr, "unknown param %s\n", setKey[i]); return 2; }
	}

	fbuf = (unsigned char *)malloc((size_t)h.frameBytes);
	mats = (float *)malloc(sizeof(float) * (size_t)h.nBones * 16);
	outPos = (float *)malloc(sizeof(float) * (size_t)h.nVerts * 3);
	nFrames = h.nFrames;
	if (seconds * 60 + 0.5 < (double)nFrames) nFrames = (long long)floor(seconds * 60 + 0.5);
	if (!noWrite) {
		fo = fopen(outPath, "wb");
		if (!fo) { fprintf(stderr, "cannot write %s\n", outPath); return 2; }
		setvbuf(fo, NULL, _IOFBF, 1 << 22);
		fwrite("SKC1", 1, 4, fo);
		u32 = (uint32_t)h.nVerts; fwrite(&u32, 4, 1, fo);
		u32 = 0; fwrite(&u32, 4, 1, fo);
	}
	if (!quiet) printf("dump %s: %d verts, %d tris, %d bones, collider %d verts, %d capsules, %lld frames (%lld used); skirt_real = %s, sim block %zu bytes\n",
		dumpPath, h.nVerts, h.nTris, h.nBones, h.nColVerts, h.nCaps, h.nFrames, nFrames, sizeof(skirt_real) == 8 ? "double" : "float", SkirtSim_RequiredSize(&as));

	tStart = nowMs();
	for (fi = 0; fi < nFrames; fi++) {
		const float *cst;
		uint32_t index, valid;
		float dt;
		int k, bad;
		double t0;
		if (fread(fbuf, 1, (size_t)h.frameBytes, h.f) != (size_t)h.frameBytes) { fprintf(stderr, "short frame %lld\n", fi); return 2; }
		memcpy(&index, fbuf, 4); memcpy(&dt, fbuf + 8, 4); memcpy(&valid, fbuf + 12, 4);
		if (!valid) continue;
		cst = (const float *)(fbuf + 80);   /* 4-byte aligned in the buffer; malloc gives 16 */
		skinMatrices(h.refInv, cst, h.nBones, mats);
		t0 = nowMs();
		if (fi == 0) SkirtSim_Reset(sim, mats);
		else SkirtSim_Step(sim, (skirt_real)dt, mats);
		stepMs += nowMs() - t0; steps++;
		SkirtSim_Read(sim, outPos);
		bad = 0;
		for (k = 0; k < h.nVerts * 3; k++) if (!isfinite(outPos[k])) { bad = 1; break; }
		if (bad) { blowups++; SkirtSim_Reset(sim, mats); SkirtSim_Read(sim, outPos); }
		if (fo) {
			fwrite(&index, 4, 1, fo);
			fwrite(outPos, sizeof(float), (size_t)h.nVerts * 3, fo);
			written++;
		}
	}
	tReadMs = nowMs() - tStart;
	if (fo) {
		const uint32_t nw = (uint32_t)written;
		fflush(fo);
		fseek(fo, 8, SEEK_SET);
		fwrite(&nw, 4, 1, fo);
		fclose(fo);
	}
	printf("frames %lld, steps %lld, %.3f ms/step (solver only), wall %.1f s, blowups %lld, contacts(last) %d, nan %d\n",
		nFrames, steps, steps ? stepMs / (double)steps : 0.0, tReadMs / 1000.0, blowups, SkirtSim_Stats(sim)->contacts, SkirtSim_Stats(sim)->nan);
	SkirtSim_Destroy(sim);
	return 0;
}
