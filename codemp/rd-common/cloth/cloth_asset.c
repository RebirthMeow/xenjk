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
/* SKA1 / SKA2 garment asset reader. See cloth_asset.h. */
#include "cloth_asset.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int ca_finite(const float *p, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) if (!isfinite(p[i])) return 0;
	return 1;
}

static uint32_t clothCrc32(const unsigned char *p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	size_t i;
	int k;
	for (i = 0; i < n; i++) {
		c ^= p[i];
		for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return c ^ 0xFFFFFFFFu;
}

#define FAIL(msg) do { if (err && errSize) snprintf(err, errSize, "%s", msg); return 0; } while (0)

int ClothAsset_Parse(const void *buf, size_t size, int checkCrc, cloth_asset_t *a, char *err, size_t errSize)
{
	const unsigned char *b = (const unsigned char *)buf;
	size_t o;
	uint32_t ver, hb, u[10];
	double c[11];
	memset(a, 0, sizeof *a);
	if (!b || size < 184 || memcmp(b, "SKA1", 4)) FAIL("not an SKA1 asset");
	memcpy(&ver, b + 4, 4); memcpy(&hb, b + 8, 4);
	if (ver != 1 && ver != 2) FAIL("unsupported SKA1 version");
	a->version = (int)ver;
	if (hb != 180) FAIL("bad SKA1 header size");
	o = 12;
	memcpy(&a->unitCm, b + o, 8); o += 8;
	memcpy(u, b + o, 40); o += 40;
	memcpy(c, b + o, 88); o += 88;
	memcpy(a->skeleton, b + o, CLOTH_NAME_LEN); a->skeleton[CLOTH_NAME_LEN] = 0; o += 32;
	{
		int k;
		for (k = 0; k < 10; k++) if (u[k] > 0x1000000u) FAIL("SKA1 count out of range");
	}
	a->nBones = (int)u[0]; a->refBone = (int)u[1]; a->nVerts = (int)u[2]; a->nTris = (int)u[3]; a->nInfl = (int)u[4];
	a->nColVerts = (int)u[5]; a->nColTris = (int)u[6]; a->nCaps = (int)u[7]; a->nRenderSurfaces = (int)u[8]; a->nRenderVerts = (int)u[9];
	if (a->nBones <= 0 || a->refBone >= a->nBones || a->nVerts <= 0 || a->nTris <= 0 || a->nInfl <= 0 || a->nInfl > 16) FAIL("SKA1 bad counts");
	if (!(a->unitCm > 0) || !isfinite(a->unitCm)) FAIL("SKA1 bad unit scale");   /* +inf passed */
	SkirtConfig_Defaults(&a->config);
	a->config.gravity[0] = c[0]; a->config.gravity[1] = c[1]; a->config.gravity[2] = c[2];
	a->config.density = c[3]; a->config.minPerParticleMass = c[4]; a->config.edge = c[5]; a->config.bend = c[6]; a->config.area = c[7];
	a->config.tetherStiffness = c[8]; a->config.drag = c[9]; a->config.lift = c[10];

#define TAKE(ptr, type, count) do { \
		const size_t bytes_ = sizeof(type) * (size_t)(count); \
		if (o + bytes_ > size) FAIL("SKA1 truncated"); \
		ptr = (const type *)(b + o); o += bytes_; } while (0)
	{
		const size_t N = (size_t)a->nVerts, NI = (size_t)a->nInfl, CV = (size_t)a->nColVerts;
		if (o + (size_t)a->nBones * 32 > size) FAIL("SKA1 truncated");
		a->boneNames = (const char *)(b + o); o += (size_t)a->nBones * 32;
		TAKE(a->refInv, float, (size_t)a->nBones * 16);
		TAKE(a->pos, float, N * 3); TAKE(a->tri, uint32_t, (size_t)a->nTris * 3); TAKE(a->maxDist, float, N); TAKE(a->animDrive, float, N);
		TAKE(a->skinBone, int32_t, N * NI); TAKE(a->skinW, float, N * NI);
		TAKE(a->colPos, float, CV * 3); TAKE(a->colTri, uint32_t, (size_t)a->nColTris * 3); TAKE(a->colSkinBone, int32_t, CV * NI); TAKE(a->colSkinW, float, CV * NI);
		TAKE(a->capA, int32_t, a->nCaps); TAKE(a->capB, int32_t, a->nCaps);
		o = (o + 7) & ~(size_t)7;
		TAKE(a->capR, double, a->nCaps);
		if (o + (size_t)a->nRenderSurfaces * 36 > size) FAIL("SKA1 truncated");
		a->rsRecords = (const char *)(b + o); o += (size_t)a->nRenderSurfaces * 36;
		o = (o + 3) & ~(size_t)3;
		if (a->version == 1) {
			TAKE(a->renderSim, uint32_t, a->nRenderVerts); TAKE(a->renderDist, float, a->nRenderVerts);
		} else {
			const unsigned char *pl;
			uint32_t plen;
			TAKE(a->renderTri, uint32_t, (size_t)a->nRenderVerts * 3); TAKE(a->renderW, float, (size_t)a->nRenderVerts * 3);
			TAKE(a->renderOff, float, a->nRenderVerts); TAKE(a->renderSgn, float, a->nRenderVerts);
			if (o + 4 > size) FAIL("SKA1 truncated");
			memcpy(&plen, b + o, 4); o += 4;
			if (plen > 65536 || o + plen > size) FAIL("SKA1 bad parameter block");
			pl = b + o; o += plen;
			a->params = (const char *)pl; a->paramsLen = (int)plen;
			o = (o + 3) & ~(size_t)3;
		}
	}
#undef TAKE
	if (o + 4 != size) FAIL("SKA1 size mismatch");
	if (checkCrc) {
		uint32_t crc;
		memcpy(&crc, b + o, 4);
		if (crc != clothCrc32(b, o)) FAIL("SKA1 crc mismatch");
	}
	{ /* index sanity: a bad file must not crash the renderer */
		int i;
		long long sum = 0;   /* u32 counts summed without overflow */
		for (i = 0; i < a->nTris * 3; i++) if (a->tri[i] >= (uint32_t)a->nVerts) FAIL("SKA1 triangle index out of range");
		for (i = 0; i < a->nColTris * 3; i++) if (a->colTri[i] >= (uint32_t)a->nColVerts) FAIL("SKA1 collider index out of range");
		for (i = 0; i < a->nVerts * a->nInfl; i++) if (a->skinBone[i] >= a->nBones) FAIL("SKA1 skin bone out of range");
		for (i = 0; i < a->nColVerts * a->nInfl; i++) if (a->colSkinBone[i] >= a->nBones) FAIL("SKA1 collider bone out of range");
		for (i = 0; i < a->nCaps; i++) if (a->capA[i] < 0 || a->capA[i] >= a->nBones || a->capB[i] < 0 || a->capB[i] >= a->nBones) FAIL("SKA1 capsule bone out of range");
		if (a->version == 1) {
			for (i = 0; i < a->nRenderVerts; i++) if (a->renderSim[i] >= (uint32_t)a->nVerts) FAIL("SKA1 render map out of range");
		} else {
			for (i = 0; i < a->nRenderVerts * 3; i++) if (a->renderTri[i] >= (uint32_t)a->nVerts) FAIL("SKA1 render map out of range");
			for (i = 0; i < a->nRenderVerts * 3; i++) if (!(a->renderW[i] >= -4.0f && a->renderW[i] <= 5.0f)) FAIL("SKA1 render weight out of range");   /* extrapolated cards past a hem */
			for (i = 0; i < a->nRenderVerts; i++) if (!(a->renderOff[i] > -64.0f && a->renderOff[i] < 64.0f)) FAIL("SKA1 render offset out of range");
		}
		for (i = 0; i < a->nRenderSurfaces; i++) { uint32_t n; memcpy(&n, a->rsRecords + i * 36 + 32, 4); sum += n; }
		if (sum != a->nRenderVerts) FAIL("SKA1 render surface counts do not add up");
	}
	{ /* the solver's inputs finite (a NaN maxDist or bind reset the instance every step, frozen) */
		if (!ca_finite(a->refInv, (size_t)a->nBones * 16) || !ca_finite(a->pos, (size_t)a->nVerts * 3) || !ca_finite(a->maxDist, a->nVerts)
			|| !ca_finite(a->animDrive, a->nVerts) || !ca_finite(a->skinW, (size_t)a->nVerts * a->nInfl)
			|| !ca_finite(a->colPos, (size_t)a->nColVerts * 3) || !ca_finite(a->colSkinW, (size_t)a->nColVerts * a->nInfl)
)
			FAIL("SKA1 non-finite value");
		for (size_t k = 0; k < 11; k++) if (!isfinite(c[k])) FAIL("SKA1 non-finite value");
		for (size_t k = 0; k < (size_t)a->nCaps; k++) if (!isfinite(a->capR[k])) FAIL("SKA1 non-finite value");
	}
	if (a->nColVerts > 0) { /* every collider vertex on a triangle (the contact tracking walks triangle lists) */
		unsigned char *used = (unsigned char *)calloc((size_t)a->nColVerts, 1);
		int i, loose = 0;
		if (!used) FAIL("SKA1 out of memory");
		for (i = 0; i < a->nColTris * 3; i++) used[a->colTri[i]] = 1;
		for (i = 0; i < a->nColVerts; i++) loose += !used[i];
		free(used);
		if (loose) FAIL("SKA1 collider vertex on no triangle");
	}
	return 1;
}

void ClothAsset_BoneName(const cloth_asset_t *a, int b, char *out, size_t outSize)
{
	size_t n = outSize - 1 < CLOTH_NAME_LEN ? outSize - 1 : CLOTH_NAME_LEN;
	memcpy(out, a->boneNames + (size_t)b * CLOTH_NAME_LEN, n);
	out[n] = 0;
}

int ClothAsset_RenderSurface(const cloth_asset_t *a, int s, char *name, size_t nameSize, int *count)
{
	int i, start = 0;
	uint32_t n;
	for (i = 0; i < s; i++) { memcpy(&n, a->rsRecords + i * 36 + 32, 4); start += (int)n; }
	if (name && nameSize) {
		size_t m = nameSize - 1 < CLOTH_NAME_LEN ? nameSize - 1 : CLOTH_NAME_LEN;
		memcpy(name, a->rsRecords + s * 36, m);
		name[m] = 0;
	}
	memcpy(&n, a->rsRecords + s * 36 + 32, 4);
	if (count) *count = (int)n;
	return start;
}

void ClothAsset_ToSkirt(const cloth_asset_t *a, skirt_asset_t *as)
{
	memset(as, 0, sizeof *as);
	as->nBones = a->nBones; as->refInv = a->refInv; as->refBone = a->refBone;
	as->nVerts = a->nVerts; as->nTris = a->nTris; as->pos = a->pos; as->tri = a->tri; as->maxDist = a->maxDist; as->animDrive = a->animDrive;
	as->nInfl = a->nInfl; as->skinBone = a->skinBone; as->skinW = a->skinW;
	as->nColVerts = a->nColVerts; as->nColTris = a->nColTris; as->colPos = a->colPos; as->colTri = a->colTri;
	as->colSkinBone = a->colSkinBone; as->colSkinW = a->colSkinW;
	as->nCapsules = a->nCaps; as->capBoneA = a->capA; as->capBoneB = a->capB; as->capRadius = a->capR;
	as->config = a->config;
}
