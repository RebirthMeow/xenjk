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

// be_collections.c -- base_enhanced 774700c game/collections.c, the list part
// the stats port uses (queues, ListInsert, ListSort and ListTrim left out).

#include "g_local.h"
#include "be_collections.h"

// ListAdd/ListInsert at the tail
void *BE_ListAdd( beList_t *list, size_t elementSize )
{
	beNode_t *node = (beNode_t *)calloc( 1, elementSize );

	if ( !node )
	{
		return NULL;
	}
	if ( !list->head )
	{
		list->head = list->tail = node;
	}
	else
	{
		beNode_t *tail = (beNode_t *)list->tail;
		tail->next = node;
		node->prev = tail;
		list->tail = node;
	}
	list->size++;
	return node;
}

// ListFind, from the head
void *BE_ListFind( beList_t *list, beListMatch_t match, void *userData )
{
	beNode_t *node;

	for ( node = (beNode_t *)list->head; node; node = (beNode_t *)node->next )
	{
		if ( match( node, userData ) )
		{
			return node;
		}
	}
	return NULL;
}

// ListRemove
void BE_ListRemove( beList_t *list, void *element )
{
	beNode_t *node = (beNode_t *)element;
	beNode_t *next = (beNode_t *)node->next, *prev = (beNode_t *)node->prev;

	if ( prev )
	{
		prev->next = next;
	}
	else
	{
		list->head = next;
	}
	if ( next )
	{
		next->prev = prev;
	}
	else
	{
		list->tail = prev;
	}
	list->size--;
	free( node );
}

// ListClear
void BE_ListClear( beList_t *list )
{
	beNode_t *node = (beNode_t *)list->head;

	while ( node )
	{
		beNode_t *next = (beNode_t *)node->next;
		free( node );
		node = next;
	}
	list->head = list->tail = NULL;
	list->size = 0;
}

// ListCopy: appends a copy of every element of `from` to `to`
void BE_ListCopy( beList_t *from, beList_t *to, size_t elementSize )
{
	beNode_t *node;

	for ( node = (beNode_t *)from->head; node; node = (beNode_t *)node->next )
	{
		beNode_t *copy = (beNode_t *)BE_ListAdd( to, elementSize );
		void *prev, *next;

		if ( !copy )
		{
			return;
		}
		prev = copy->prev;
		next = copy->next;
		memcpy( copy, node, elementSize );
		copy->prev = prev;
		copy->next = next;
	}
}

// ListIterate (forward only)
void BE_ListIterate( beList_t *list, beIter_t *iter )
{
	iter->current = list->head;
}

// IteratorHasNext
qboolean BE_IterHasNext( beIter_t *iter )
{
	return iter->current != NULL;
}

// IteratorNext
void *BE_IterNext( beIter_t *iter )
{
	beNode_t *node = (beNode_t *)iter->current;

	if ( !node )
	{
		return NULL;
	}
	iter->current = node->next;
	return node;
}
