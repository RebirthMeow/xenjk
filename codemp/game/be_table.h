/*
===========================================================================
Copyright (C) 2014 - 2026, base_enhanced contributors (TheSil, deathsythe47, Avygeil)
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative, and carries code ported from
base_enhanced (https://github.com/avygeil/base_enhanced, GPLv2).

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

// be_table.h -- base_enhanced's text-table renderer (g_table.c), quarantined
// for the stats port (be_stats.c).
//
// Define every row first (Table_DefineRow), then the columns: defining a
// column calls its callback once per row and keeps the strings, so row
// contexts may be freed afterwards. A column with no non-empty cell is
// dropped. Write the table with BE_TableWrite.

#pragma once

#include "be_collections.h"

typedef const char *( *beTableCell_t )( void *rowContext, void *columnContext );

typedef struct beTable_s {
	beList_t		columnList;
	beList_t		rowList;
	qboolean		alternateColors;
	int				lastColumnIdAssigned;
} beTable_t;

beTable_t	*BE_TableInit( qboolean alternateColors );						// Table_Initialize
void		BE_TableDestroy( beTable_t *t );								// Table_Destroy
void		BE_TableDefineRow( beTable_t *t, void *context );				// Table_DefineRow
void		BE_TableDefineColumn( beTable_t *t, const char *title, beTableCell_t callback, void *columnContext,
				qboolean leftAlign, int dividerColor, int maxLen );			// Table_DefineColumn
void		BE_TableAddHorizontalRule( beTable_t *t, int customColor );	// Table_AddHorizontalRule
void		BE_TableWrite( beTable_t *t, char *buf, size_t bufSize, qboolean showHeader, int customHeaderColor );	// Table_WriteToBuffer
