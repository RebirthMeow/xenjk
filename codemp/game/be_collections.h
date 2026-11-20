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

// be_collections.h -- base_enhanced's linked list (collections.h, Avygeil),
// quarantined for the stats port (be_stats.c, be_table.c).
//
// An element type extends the list by declaring a beNode_t as its FIRST
// member. BE_ListAdd allocates (calloc) and appends an element of the given
// size; BE_ListRemove/BE_ListClear free. Lists start zeroed. Never change a
// list while iterating it. Names carry a BE_ prefix so nothing collides with
// other ports of the same helpers.

#pragma once

#include <stddef.h>

typedef struct beNode_s {
	void		*next;
	void		*prev;
} beNode_t;

typedef struct beList_s {
	void		*head;
	void		*tail;
	int			size;
} beList_t;

typedef struct beIter_s {
	void		*current;
} beIter_t;

typedef qboolean ( *beListMatch_t )( void *element, void *userData );

void		*BE_ListAdd( beList_t *list, size_t elementSize );					// ListAdd; NULL if out of memory
void		*BE_ListFind( beList_t *list, beListMatch_t match, void *userData );	// ListFind from the head
void		BE_ListRemove( beList_t *list, void *element );						// ListRemove
void		BE_ListClear( beList_t *list );										// ListClear
void		BE_ListCopy( beList_t *from, beList_t *to, size_t elementSize );		// ListCopy
void		BE_ListIterate( beList_t *list, beIter_t *iter );						// ListIterate, forward
qboolean	BE_IterHasNext( beIter_t *iter );									// IteratorHasNext
void		*BE_IterNext( beIter_t *iter );										// IteratorNext
