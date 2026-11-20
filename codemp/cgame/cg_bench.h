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

// cg_bench.h -- the renderer bench's frame-time logger and fixed camera (cg_bench.c). Hooks: CG_Bench_Frame at the
// top of CG_DrawActiveFrame, CG_Bench_Camera after CG_CalcViewValues, the two commands in cg_consolecmds.c.

#pragma once

void CG_Bench_Frame( void );
void CG_Bench_Camera( void );
void CG_Bench_Cam_f( void );		// benchcam
void CG_Bench_View_f( void );		// benchview
