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
/* hair_asset.c -- HCH1 v2 parser (see hair_asset.h, docs/hair/FORMAT.md). */
#include "hair_asset.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HCH_HEADER 128
#define HCH_SPRING_HEAD 44   /* i32 nSprings, f32 dt, f32 basis[9] */
#define HCH_SPRING 40

/* the zlib CRC-32, eight bytes a step (slicing-by-8): a 10 MB asset's check was 40+ ms bit by bit */
static unsigned crc_table[8][256];

static void crc32_init(void)
{
	for (unsigned n = 0; n < 256; n++) {
		unsigned c = n;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
		crc_table[0][n] = c;
	}
	for (unsigned n = 0; n < 256; n++)
		for (int t = 1; t < 8; t++)
			crc_table[t][n] = (crc_table[t - 1][n] >> 8) ^ crc_table[0][crc_table[t - 1][n] & 0xff];
}

static unsigned crc32_le(const unsigned char *p, size_t n)
{
	unsigned c = 0xffffffffu;
	if (!crc_table[0][1])
		crc32_init();
	for (; n >= 8; n -= 8, p += 8) {
		const unsigned a = c ^ ((unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24);
		const unsigned b = (unsigned)p[4] | (unsigned)p[5] << 8 | (unsigned)p[6] << 16 | (unsigned)p[7] << 24;
		c = crc_table[7][a & 0xff] ^ crc_table[6][(a >> 8) & 0xff] ^ crc_table[5][(a >> 16) & 0xff] ^ crc_table[4][a >> 24]
			^ crc_table[3][b & 0xff] ^ crc_table[2][(b >> 8) & 0xff] ^ crc_table[1][(b >> 16) & 0xff] ^ crc_table[0][b >> 24];
	}
	for (; n; n--, p++)
		c = (c >> 8) ^ crc_table[0][(c ^ *p) & 0xff];
	return c ^ 0xffffffffu;
}

static int geti(const unsigned char *b, size_t o) { int v; memcpy(&v, b + o, 4); return v; }
static float getf(const unsigned char *b, size_t o) { float v; memcpy(&v, b + o, 4); return v; }

static int fail(char *err, size_t errLen, const char *msg)
{
	if (err && errLen)
		snprintf(err, errLen, "%s", msg);
	return 0;
}

void HairAsset_Free(hairAsset_t *h)
{
	free(h->a.particles); free(h->a.cones); free(h->a.links); free(h->a.receivers); free(h->a.repulsors);
	free(h->a.pairs); free(h->a.bones); free(h->a.mirrors);
	free(h->gameBoneNames); free(h->renderSurfs); free(h->renderEntries); free(h->boneHost); free(h->springs);
	memset(h, 0, sizeof(*h));
}

int HairAsset_Parse(const void *buf, size_t len, hairAsset_t *out, char *err, size_t errLen)
{
	return HairAsset_ParseEx(buf, len, out, err, errLen, 0);
}

int HairAsset_ParseEx(const void *buf, size_t len, hairAsset_t *out, char *err, size_t errLen, int options)
{
	const unsigned char *b = (const unsigned char *)buf;
	hcAsset *a = &out->a;
	size_t o = HCH_HEADER, need;
	unsigned crc;

	memset(out, 0, sizeof(*out));
	if (len < HCH_HEADER + 4 || memcmp(b, "HCH1", 4))
		return fail(err, errLen, "not an HCH1 hair-chains asset");
	if (geti(b, 4) != 2 || geti(b, 8) != HCH_HEADER)
		return fail(err, errLen, "unsupported HCH1 version (need 2)");
	memcpy(&crc, b + len - 4, 4);
	if (crc != crc32_le(b, len - 4))
		return fail(err, errLen, "crc mismatch");

	a->nParticles = geti(b, 12); a->nCones = geti(b, 16); a->nLinks = geti(b, 20); a->nReceivers = geti(b, 24);
	a->nRepulsors = geti(b, 28); a->nPairs = geti(b, 32); a->nBones = geti(b, 36); a->nMirrors = geti(b, 40);
	a->hostBone = -1;   /* bound by name (hostName) */
	a->substeps = geti(b, 48); a->lateralIter = geti(b, 52); a->collisionIter = geti(b, 56);
	a->dt = getf(b, 60);
	for (int k = 0; k < 3; k++)
		a->gravity[k] = getf(b, 64 + 4 * k);
	a->unitsPerCm = getf(b, 76);
	out->nRenderSurfs = geti(b, 80);
	out->nRenderEntries = geti(b, 84);
	memcpy(out->hostName, b + 88, HAIR_NAME_LEN);
	out->hostName[HAIR_NAME_LEN - 1] = 0;
	a->flags = geti(b, 120);
	out->nGameBones = geti(b, 124);
	/* no particles is a springs-only asset (body springs, no chains): checked below once nSprings is known */
	if (a->nParticles < 0 || (a->nParticles == 0 && !(a->flags & HAIR_ASSET_SPRINGS)) || a->nCones < 0 || a->nLinks < 0 || a->nReceivers < 0 || a->nRepulsors < 0 || a->nPairs < 0
		|| a->nBones <= 0 || a->nMirrors < 0 || out->nGameBones < 0 || out->nRenderSurfs < 0 || out->nRenderEntries < 0
		|| a->substeps < 1 || a->lateralIter < 1 || a->collisionIter < 1 || !(a->dt > 0))
		return fail(err, errLen, "bad header counts");
	/* upper bounds. The file is read from any pk3, and HC_Init sizes a table by nBones squared and
	   HC_Step loops substeps x iterations, so a hostile count overflows the heap or hangs the client. The bones'
	   ceiling is the GPU palette (HAIR_MAX_PALETTE 256), checked later in Hair_BindBones but after HC_Init; the
	   others sit far above every shipped asset (179 assets: 199 bones, 161 particles and cones,
	   6,018 pairs, at most 4 substeps or iterations) */
	if (a->nBones > 256 || a->nParticles > 4096 || a->nCones > 4096 || a->nPairs > 65536
		|| a->substeps > 16 || a->lateralIter > 16 || a->collisionIter > 16)
		return fail(err, errLen, "header counts over the engine's limits");
	need = HCH_HEADER + (size_t)a->nParticles * 48 + (size_t)a->nCones * 40 + (size_t)a->nLinks * 20
		+ (size_t)a->nReceivers * 20 + (size_t)a->nRepulsors * 48 + (size_t)a->nPairs * 8 + (size_t)a->nBones * 100
		+ (size_t)a->nMirrors * 8 + (size_t)out->nGameBones * 32 + (size_t)out->nRenderSurfs * 40
		+ (size_t)out->nRenderEntries * 40 + 4;
	if (a->flags & HAIR_ASSET_BONE_HOSTS)
		need += (size_t)a->nBones * 4;
	if (a->flags & HAIR_ASSET_SPRINGS) {
		/* the block starts where the crc would; its own count sizes it */
		if (len < need + HCH_SPRING_HEAD)
			return fail(err, errLen, "size does not match the header counts");
		out->nSprings = geti(b, need - 4);
		if (out->nSprings < 0 || out->nSprings > 4096)
			return fail(err, errLen, "bad spring count");
		need += HCH_SPRING_HEAD + (size_t)out->nSprings * HCH_SPRING;
	}
	if (need != len)
		return fail(err, errLen, "size does not match the header counts");
	if (a->nParticles == 0 && out->nSprings == 0)
		return fail(err, errLen, "no particles and no springs");

	a->particles = calloc(a->nParticles ? a->nParticles : 1, sizeof(hcParticle));
	a->cones = calloc(a->nCones ? a->nCones : 1, sizeof(hcCone));
	a->links = calloc(a->nLinks ? a->nLinks : 1, sizeof(hcLink));
	a->receivers = calloc(a->nReceivers ? a->nReceivers : 1, sizeof(hcReceiver));
	a->repulsors = calloc(a->nRepulsors ? a->nRepulsors : 1, sizeof(hcRepulsor));
	a->pairs = calloc(a->nPairs ? a->nPairs : 1, sizeof(*a->pairs));
	a->bones = calloc(a->nBones, sizeof(hcBone));
	a->mirrors = calloc(a->nMirrors ? a->nMirrors : 1, sizeof(*a->mirrors));
	out->gameBoneNames = calloc(out->nGameBones ? out->nGameBones : 1, HAIR_NAME_LEN);
	out->renderSurfs = calloc(out->nRenderSurfs ? out->nRenderSurfs : 1, sizeof(hairRenderSurf_t));
	out->renderEntries = calloc(out->nRenderEntries ? out->nRenderEntries : 1, sizeof(hairRenderEntry_t));
	out->boneHost = calloc(a->nBones, sizeof(int));
	out->springs = calloc(out->nSprings ? out->nSprings : 1, sizeof(hsSpring));
	if (!a->particles || !a->cones || !a->links || !a->receivers || !a->repulsors || !a->pairs || !a->bones || !a->mirrors
		|| !out->gameBoneNames || !out->renderSurfs || !out->renderEntries || !out->boneHost || !out->springs) {
		HairAsset_Free(out);
		return fail(err, errLen, "out of memory");
	}

	for (int i = 0; i < a->nParticles; i++, o += 48) {
		hcParticle *p = &a->particles[i];
		p->bone = geti(b, o); p->group = geti(b, o + 4);
		p->radius = getf(b, o + 8); p->gravityScale = getf(b, o + 12); p->damping = getf(b, o + 16);
		p->velocityDamping = getf(b, o + 20); p->constraintDamping = getf(b, o + 24); p->friction = getf(b, o + 28);
		p->bounciness = getf(b, o + 32); p->parkRate = getf(b, o + 36); p->fictitious = getf(b, o + 40);
		p->centrifugal = getf(b, o + 44);
	}
	for (int i = 0; i < a->nCones; i++, o += 40) {
		hcCone *c = &a->cones[i];
		c->child = geti(b, o); c->parent = geti(b, o + 4); c->anchorBone = geti(b, o + 8);
		c->limit = getf(b, o + 12); c->inner = getf(b, o + 16); c->outer = getf(b, o + 20); c->stretch = getf(b, o + 24);
		c->compress = getf(b, o + 28); c->parentStrength = getf(b, o + 32); c->rest = getf(b, o + 36);
	}
	for (int i = 0; i < a->nLinks; i++, o += 20) {
		hcLink *l = &a->links[i];
		l->a = geti(b, o); l->b = geti(b, o + 4); l->stretch = getf(b, o + 8); l->compress = getf(b, o + 12);
		l->rest = getf(b, o + 16);
	}
	for (int i = 0; i < a->nReceivers; i++, o += 20) {
		hcReceiver *r = &a->receivers[i];
		r->n = geti(b, o); r->p[0] = geti(b, o + 4); r->p[1] = geti(b, o + 8);
		r->r[0] = getf(b, o + 12); r->r[1] = getf(b, o + 16);
	}
	for (int i = 0; i < a->nRepulsors; i++, o += 48) {
		hcRepulsor *r = &a->repulsors[i];
		r->n = geti(b, o); r->gameBone[0] = geti(b, o + 4); r->gameBone[1] = geti(b, o + 8); r->flags = geti(b, o + 12);
		for (int k = 0; k < 3; k++) {
			r->bind[0][k] = getf(b, o + 16 + 4 * k);
			r->bind[1][k] = getf(b, o + 28 + 4 * k);
		}
		r->r[0] = getf(b, o + 40); r->r[1] = getf(b, o + 44);
	}
	for (int i = 0; i < a->nPairs; i++, o += 8) {
		a->pairs[i][0] = geti(b, o); a->pairs[i][1] = geti(b, o + 4);
	}
	for (int i = 0; i < a->nBones; i++, o += 100) {
		memcpy(a->bones[i].name, b + o, 32);
		a->bones[i].name[31] = 0;
		a->bones[i].parent = geti(b, o + 32);
		for (int k = 0; k < 16; k++)
			a->bones[i].bind[k] = getf(b, o + 36 + 4 * k);
	}
	for (int i = 0; i < a->nMirrors; i++, o += 8) {
		a->mirrors[i][0] = geti(b, o); a->mirrors[i][1] = geti(b, o + 4);
	}
	for (int i = 0; i < out->nGameBones; i++, o += 32) {
		memcpy(out->gameBoneNames[i], b + o, 32);
		out->gameBoneNames[i][31] = 0;
	}
	for (int i = 0; i < out->nRenderSurfs; i++, o += 40) {
		hairRenderSurf_t *s = &out->renderSurfs[i];
		memcpy(s->name, b + o, 32);
		s->name[31] = 0;
		s->numVerts = geti(b, o + 32);
		s->lod = geti(b, o + 36);
	}
	for (int i = 0; i < out->nRenderEntries; i++, o += 40) {
		hairRenderEntry_t *e = &out->renderEntries[i];
		e->surf = geti(b, o); e->vert = geti(b, o + 4);
		for (int k = 0; k < 4; k++) {
			e->bone[k] = geti(b, o + 8 + 4 * k);
			e->w[k] = getf(b, o + 24 + 4 * k);
		}
	}
	for (int i = 0; i < a->nBones; i++) {
		/* a root bone hangs from its host; every other bone from its root, so it takes the root's host */
		if (a->flags & HAIR_ASSET_BONE_HOSTS) {
			out->boneHost[i] = geti(b, o);
			o += 4;
		} else {
			out->boneHost[i] = -1;
		}
	}
	if (a->flags & HAIR_ASSET_SPRINGS) {
		o += 4;   /* nSprings, read above */
		out->springDt = getf(b, o);
		for (int k = 0; k < 9; k++)
			out->springBasis[k] = getf(b, o + 4 + 4 * k);
		o += HCH_SPRING_HEAD - 4;
		for (int i = 0; i < out->nSprings; i++, o += HCH_SPRING) {
			hsSpring *s = &out->springs[i];
			s->bone = geti(b, o); s->flags = geti(b, o + 4);
			s->stiffness = getf(b, o + 8); s->damping = getf(b, o + 12); s->maxDisplacement = getf(b, o + 16);
			s->resetThresh = getf(b, o + 20); s->alpha = getf(b, o + 24);
			for (int k = 0; k < 3; k++)
				s->pivot[k] = getf(b, o + 28 + 4 * k);
		}
	}

	/* indices in range */
	for (int i = 0; i < a->nParticles; i++)
		if (a->particles[i].bone < 0 || a->particles[i].bone >= a->nBones)
			goto bad;
	for (int i = 0; i < a->nCones; i++) {
		const hcCone *c = &a->cones[i];
		if (c->child < 0 || c->child >= a->nParticles || c->parent >= a->nParticles
			|| (c->parent < 0 && (c->anchorBone < 0 || c->anchorBone >= a->nBones)))
			goto bad;
	}
	for (int i = 0; i < a->nLinks; i++)
		if (a->links[i].a < 0 || a->links[i].a >= a->nParticles || a->links[i].b < 0 || a->links[i].b >= a->nParticles)
			goto bad;
	for (int i = 0; i < a->nReceivers; i++) {
		const hcReceiver *r = &a->receivers[i];
		if (r->n < 1 || r->n > 2 || r->p[0] < 0 || r->p[0] >= a->nParticles || r->p[1] < 0 || r->p[1] >= a->nParticles)
			goto bad;
	}
	for (int i = 0; i < a->nRepulsors; i++) {
		const hcRepulsor *r = &a->repulsors[i];
		if (r->n < 1 || r->n > 2)
			goto bad;
		for (int k = 0; k < r->n; k++)
			if ((r->gameBone[k] < 0 && !(r->gameBone[k] == -1 && (options & HAIR_PARSE_OFFLINE) && (a->flags & 1)))
				|| r->gameBone[k] >= out->nGameBones)
				goto bad;
	}
	for (int i = 0; i < a->nPairs; i++)
		if (a->pairs[i][0] < 0 || a->pairs[i][0] >= a->nReceivers || a->pairs[i][1] < 0 || a->pairs[i][1] >= a->nRepulsors)
			goto bad;
	for (int i = 0; i < a->nBones; i++) {
		if (a->bones[i].parent >= i || out->boneHost[i] < -1 || out->boneHost[i] >= out->nGameBones)
			goto bad;
		if (a->bones[i].parent >= 0)
			out->boneHost[i] = out->boneHost[a->bones[i].parent];
	}
	for (int i = 0; i < a->nMirrors; i++)
		if (a->mirrors[i][0] < 0 || a->mirrors[i][0] >= a->nBones || a->mirrors[i][1] < 0 || a->mirrors[i][1] >= a->nBones)
			goto bad;
	for (int i = 0; i < out->nRenderEntries; i++) {
		const hairRenderEntry_t *e = &out->renderEntries[i];
		if (e->surf < 0 || e->surf >= out->nRenderSurfs || e->vert < 0 || e->vert >= out->renderSurfs[e->surf].numVerts)
			goto bad;
		for (int k = 0; k < 4; k++) {
			if (e->bone[k] >= a->nBones)
				goto bad;
			if (e->bone[k] <= -2 && (!(a->flags & HAIR_ASSET_GAME_REFS) || -2 - e->bone[k] >= out->nGameBones))
				goto bad;
		}
	}
	/* a spring moves one root bone that no chain uses, once */
	if ((a->flags & HAIR_ASSET_SPRINGS) && !(out->springDt > 0))
		goto bad;
	for (int i = 0; i < out->nSprings; i++) {
		const int sb = out->springs[i].bone;
		if (sb < 0 || sb >= a->nBones || a->bones[sb].parent != -1)
			goto bad;
		for (int j = 0; j < i; j++)
			if (out->springs[j].bone == sb)
				goto bad;
		for (int p = 0; p < a->nParticles; p++)
			if (a->particles[p].bone == sb)
				goto bad;
		for (int c = 0; c < a->nCones; c++)
			if (a->cones[c].parent < 0 && a->cones[c].anchorBone == sb)
				goto bad;
	}
	/* what the engine assumes and nothing checked. Bind matrices rigid (M4_InvRigid, the joint box); render
	   weights in [0, 1] (they become bytes); cone constants finite (a NaN rest length resets the instance every step);
	   cones root to tip (a cone's parent particle is not solved by a later cone). The 179 shipped assets
	   pass all four (worst bind off rigid by 6.3e-5) */
	for (int k = 0; k < a->nBones; k++) {
		const float *m = a->bones[k].bind;
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++) {
				const double d = (double)m[r * 4] * m[c * 4] + (double)m[r * 4 + 1] * m[c * 4 + 1] + (double)m[r * 4 + 2] * m[c * 4 + 2] - (r == c);
				if (!(fabs(d) <= 1e-3)) {
					HairAsset_Free(out);
					return fail(err, errLen, "a bind matrix is not rigid");
				}
			}
	}
	for (int k = 0; k < out->nRenderEntries; k++)
		for (int r = 0; r < 4; r++)
			if (!(out->renderEntries[k].w[r] >= 0.0f && out->renderEntries[k].w[r] <= 1.0001f)) {
				HairAsset_Free(out);
				return fail(err, errLen, "a render weight is outside [0, 1]");
			}
	for (int c = 0; c < a->nCones; c++) {
		const hcCone *cn = &a->cones[c];
		if (!isfinite(cn->rest) || !isfinite(cn->limit) || !isfinite(cn->stretch) || !isfinite(cn->compress)
			|| !isfinite(cn->inner) || !isfinite(cn->outer) || !isfinite(cn->parentStrength)) {
			HairAsset_Free(out);
			return fail(err, errLen, "a cone constant is not finite");
		}
		if (cn->parent >= 0)
			for (int j = c + 1; j < a->nCones; j++)
				if (a->cones[j].child == cn->parent) {
					HairAsset_Free(out);
					return fail(err, errLen, "cones are not in root-to-tip order");
				}
	}
	return 1;
bad:
	HairAsset_Free(out);
	return fail(err, errLen, "an index is out of range");
}
