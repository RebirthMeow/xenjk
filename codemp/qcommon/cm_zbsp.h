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

#pragma once

// xenjk collision option C: XBSZ chunk BSPs (cm_zbsp.cpp, tools/levelport/colzbsp.py)

// the buffer is an XBSZ file
bool			CM_IsXbsz( const unsigned char *buf, int len );
// the original RBSP bytes (malloc'd; the caller frees), or NULL with err filled
unsigned char	*CM_XbszDecode( const unsigned char *buf, int len, int *outLen, char *err, int errLen );
