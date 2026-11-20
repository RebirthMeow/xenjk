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
Garment cloth: rd-vanilla back end. The common glue (rd-common/tr_cloth.cpp) builds the model-space draw buffer;
this copies it over the CPU-skinned positions and normals in tess.
*/
#include "tr_local.h"
#include "rd-common/tr_cloth_local.h"

const mdxmHeader_t *R_ClothModelMdxm( const model_t *mod ) {
	return mod->mdxm;
}

void R_ClothRBFreeAsset( clothModelAsset_t *as ) {
	(void)as;
}

void R_ClothRBFreeInst( clothInst_t *ci ) {
	(void)ci;
}

void R_ClothRBRegister( void ) {
}

void R_ClothRBShutdown( void ) {
}

bool R_ClothRBUseGpu( clothInst_t *ci ) {
	(void)ci;
	return false;
}

bool R_ClothRBGpuStep( clothInst_t *ci, const skirt_frame_t *fr, const float *capOld, const float *capNew,
	const float *mats, const float G[12], int relSlot ) {
	(void)ci; (void)fr; (void)capOld; (void)capNew; (void)mats; (void)G; (void)relSlot;
	return false;
}

void R_ClothRBGpuDraw( clothInst_t *ci, CBoneCache *bc, float t, int relZ, int relA, int relB, float s, bool lead2,
	const float *Fc ) {
	(void)ci; (void)bc; (void)t; (void)relZ; (void)relA; (void)relB; (void)s; (void)lead2; (void)Fc;
}

void RB_ClothSurface( const CBoneCache *bc, const mdxmSurface_t *surface, int baseVertex ) {
	if ( !r_cloth || !r_cloth->integer ) return;
	clothInst_t *ci = R_ClothFindInst( bc );
	if ( !ci || !ci->ready ) return;
	const clothModelAsset_t *as = ci->asset;
	const int idx = surface->thisSurfaceIndex;
	if ( idx < 0 || idx >= (int)as->surfToRS.size() ) return;
	const int rs = as->surfToRS[idx];
	if ( rs < 0 || as->rsLod0[rs] != surface ) return;  // LOD 0 only
	if ( !R_SimDrawable( SIMCHECK_CLOTH, true, &ci->gate, backEnd.viewParms.frameCount ) ) return;   // not this frame's
	const int start = as->rsStart[rs];
	const float *dp = &ci->drawPos[0], *dn = &ci->drawNrm[0];
	for ( int v = 0; v < as->rsCount[rs]; v++ ) {
		R_ClothRenderVertex( as, dp, dn, start + v, tess.xyz[baseVertex + v], tess.normal[baseVertex + v] );
	}
}
