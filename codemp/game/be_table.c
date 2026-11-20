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

// be_table.c -- base_enhanced 774700c game/g_table.c: the table renderer and
// CompressColors. The account/pool/map callbacks are left out (no database).

#include "g_local.h"
#include "be_table.h"

#define TABLE_MAX_COLUMNS	(64)

typedef struct {
	beNode_t		node;
	void			*context;
	char			str[TABLE_MAX_COLUMNS][128];
	qboolean		isHorizontalRule;
	int				customHorizontalRuleColor;
} beTableRow_t;

typedef struct {
	beNode_t		node;
	char			title[128];
	qboolean		leftAlign;
	int				maxLen;
	int				longestPrintLen;
	int				columnId;
	int				dividerColor;
} beTableColumn_t;

// Table_Initialize
beTable_t *BE_TableInit( qboolean alternateColors )
{
	beTable_t *t = (beTable_t *)calloc( 1, sizeof( beTable_t ) );

	if ( !t )
	{
		return NULL;
	}
	t->lastColumnIdAssigned = -1;
	t->alternateColors = alternateColors;
	return t;
}

// Table_Destroy
void BE_TableDestroy( beTable_t *t )
{
	if ( !t )
	{
		return;
	}
	BE_ListClear( &t->rowList );
	BE_ListClear( &t->columnList );
	free( t );
}

// Table_DefineRow: all rows come before any column
void BE_TableDefineRow( beTable_t *t, void *context )
{
	beTableRow_t *row;

	if ( !t )
	{
		return;
	}
	row = (beTableRow_t *)BE_ListAdd( &t->rowList, sizeof( beTableRow_t ) );
	if ( !row )
	{
		return;
	}
	row->context = context;
	row->isHorizontalRule = qfalse;
	row->customHorizontalRuleColor = -1;
}

// Table_DefineColumn: dividerColor 0..9 draws a | to the right of the column
void BE_TableDefineColumn( beTable_t *t, const char *title, beTableCell_t callback, void *columnContext,
	qboolean leftAlign, int dividerColor, int maxLen )
{
	beTableColumn_t *column;
	beIter_t iter;
	qboolean gotOne = qfalse;

	if ( !t || !callback || t->lastColumnIdAssigned + 1 >= TABLE_MAX_COLUMNS )
	{
		return;
	}
	column = (beTableColumn_t *)BE_ListAdd( &t->columnList, sizeof( beTableColumn_t ) );
	if ( !column )
	{
		return;
	}
	column->columnId = ++t->lastColumnIdAssigned;
	Q_strncpyz( column->title, title, sizeof( column->title ) );
	column->leftAlign = leftAlign;
	column->maxLen = maxLen;
	column->longestPrintLen = Q_PrintStrlen( title );
	column->dividerColor = ( dividerColor >= 0 && dividerColor <= 9 ) ? dividerColor : -1;

	BE_ListIterate( &t->rowList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		beTableRow_t *row = (beTableRow_t *)BE_IterNext( &iter );
		const char *result;

		if ( row->isHorizontalRule )
		{
			continue;
		}
		result = callback( row->context, columnContext );
		if ( VALIDSTRING( result ) )
		{
			int printLen;

			Q_strncpyz( row->str[column->columnId], result, sizeof( row->str[column->columnId] ) );
			printLen = Q_PrintStrlen( row->str[column->columnId] );
			if ( printLen > column->longestPrintLen )
			{
				column->longestPrintLen = printLen;
			}
			gotOne = qtrue;
		}
	}

	// not a single result for this column: delete it entirely
	if ( !gotOne )
	{
		BE_ListRemove( &t->columnList, column );
		--t->lastColumnIdAssigned;
	}
}

static void BE_TableAddSpaces( char *buf, size_t bufSize, int count )
{
	int i;

	for ( i = 0; i < count; i++ )
	{
		Q_strcat( buf, bufSize, " " );
	}
}

// WriteColumnHeader: the titles and the line of dashes under them
static void BE_TableWriteHeader( beTable_t *t, char *buf, size_t bufSize, int customHeaderColor )
{
	beIter_t iter;

	BE_ListIterate( &t->columnList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		beTableColumn_t *column = (beTableColumn_t *)BE_IterNext( &iter );

		Q_strcat( buf, bufSize, column->title );
		BE_TableAddSpaces( buf, bufSize, column->longestPrintLen - Q_PrintStrlen( column->title ) + 1 );
		if ( BE_IterHasNext( &iter ) && column->dividerColor >= 0 )
		{
			Q_strcat( buf, bufSize, va( "^%d| ^7", column->dividerColor ) );
		}
	}
	Q_strcat( buf, bufSize, "\n" );

	if ( customHeaderColor >= 0 && customHeaderColor <= 9 )
	{
		Q_strcat( buf, bufSize, va( "^%d", customHeaderColor ) );
	}
	else
	{
		Q_strcat( buf, bufSize, "^9" );
	}

	BE_ListIterate( &t->columnList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		beTableColumn_t *column = (beTableColumn_t *)BE_IterNext( &iter );
		int i;

		for ( i = 0; i < column->longestPrintLen; i++ )
		{
			Q_strcat( buf, bufSize, "-" );
		}
		Q_strcat( buf, bufSize, " " );
		if ( BE_IterHasNext( &iter ) && column->dividerColor >= 0 )
		{
			if ( customHeaderColor == column->dividerColor )
			{
				Q_strcat( buf, bufSize, "| " );
			}
			else
			{
				Q_strcat( buf, bufSize, va( "^%d| ^%d", column->dividerColor, customHeaderColor ) );
			}
		}
	}
	Q_strcat( buf, bufSize, "^7\n" );
}

// WriteCell
static void BE_TableWriteCell( beTable_t *t, beTableRow_t *row, beTableColumn_t *column, qboolean grey, char *buf, size_t bufSize )
{
	const char *cell = row->str[column->columnId];
	int printLen = VALIDSTRING( cell ) ? Q_PrintStrlen( cell ) : 0;
	qboolean lastColumn = ( column->columnId == t->lastColumnIdAssigned ) ? qtrue : qfalse;

	if ( column->leftAlign )
	{ // "text  nextThing"
		Q_strcat( buf, bufSize, grey ? "^9" : "^7" );
		if ( VALIDSTRING( cell ) )
		{
			Q_strcat( buf, bufSize, cell );
		}
		if ( !lastColumn )
		{
			BE_TableAddSpaces( buf, bufSize, column->longestPrintLen - printLen + 1 );
			if ( column->dividerColor >= 0 )
			{
				Q_strcat( buf, bufSize, va( "^%d| ", column->dividerColor ) );
			}
		}
	}
	else
	{ // " text nextThing"
		BE_TableAddSpaces( buf, bufSize, column->longestPrintLen - printLen );
		Q_strcat( buf, bufSize, grey ? "^9" : "^7" );
		if ( VALIDSTRING( cell ) )
		{
			Q_strcat( buf, bufSize, cell );
		}
		if ( !lastColumn )
		{
			if ( column->dividerColor >= 0 )
			{
				Q_strcat( buf, bufSize, va( " ^%d| ", column->dividerColor ) );
			}
			else
			{
				Q_strcat( buf, bufSize, " " );
			}
		}
	}
}

static qboolean BE_TableColumnIdMatches( void *element, void *userData )
{
	return ( (beTableColumn_t *)element )->columnId == *(int *)userData ? qtrue : qfalse;
}

// WriteHorizontalRule
static void BE_TableWriteRule( beTable_t *t, char *buf, size_t bufSize, int customColor )
{
	beIter_t iter;

	if ( customColor >= 0 && customColor <= 9 )
	{
		Q_strcat( buf, bufSize, va( "^%d", customColor ) );
	}
	else
	{
		Q_strcat( buf, bufSize, "^9" );
	}

	BE_ListIterate( &t->columnList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		beTableColumn_t *column = (beTableColumn_t *)BE_IterNext( &iter );
		int i;

		for ( i = 0; i < column->longestPrintLen; i++ )
		{
			Q_strcat( buf, bufSize, "-" );
		}
		Q_strcat( buf, bufSize, " " );
		if ( BE_IterHasNext( &iter ) && column->dividerColor >= 0 )
		{
			if ( column->dividerColor == customColor )
			{
				Q_strcat( buf, bufSize, "| " );
			}
			else
			{
				Q_strcat( buf, bufSize, va( "^%d| ^%d", column->dividerColor, customColor ) );
			}
		}
	}
}

// Table_AddHorizontalRule: a row of automatically sized lines
void BE_TableAddHorizontalRule( beTable_t *t, int customColor )
{
	beTableRow_t *row;

	if ( !t )
	{
		return;
	}
	row = (beTableRow_t *)BE_ListAdd( &t->rowList, sizeof( beTableRow_t ) );
	if ( !row )
	{
		return;
	}
	row->isHorizontalRule = qtrue;
	row->customHorizontalRuleColor = customColor;
}

// CompressColors: drops redundant colour codes (^1^3^1hello^1th^4^1ere -> ^1hellothere)
static void BE_TableCompressColors( const char *in, char *out, int outSize )
{
	char color = '7', pendingColor = '\0';
	int remaining = outSize;
	char *w = out;
	const char *p;

	if ( !out || !outSize )
	{
		return;
	}
	*out = '\0';
	if ( !VALIDSTRING( in ) )
	{
		return;
	}

	for ( p = in; *p; p++ )
	{
		if ( remaining == 1 )
		{ // room for the terminator
			break;
		}
		if ( Q_IsColorString( p ) )
		{
			char newColor = *( p + 1 );
			pendingColor = ( newColor == color ) ? '\0' : newColor;
			p++;
		}
		else if ( *p == ' ' || *p == '\n' )
		{
			*w++ = *p;
			remaining--;
		}
		else if ( pendingColor && remaining > 2 )
		{
			*w++ = '^';
			*w++ = pendingColor;
			color = pendingColor;
			pendingColor = '\0';
			remaining -= 2;
			if ( remaining > 1 )
			{
				*w++ = *p;
				remaining--;
			}
			else
			{
				break;
			}
		}
		else if ( remaining > 1 )
		{
			*w++ = *p;
			remaining--;
		}
		else
		{
			break;
		}
	}
	*w = '\0';
}

// Table_WriteToBuffer
void BE_TableWrite( beTable_t *t, char *buf, size_t bufSize, qboolean showHeader, int customHeaderColor )
{
	const size_t tempSize = bufSize * 2;
	char *temp;
	qboolean grey = qfalse;
	beIter_t iter;

	if ( !t || !buf || !bufSize )
	{
		return;
	}
	temp = (char *)calloc( 1, tempSize );
	if ( !temp )
	{
		return;
	}

	if ( showHeader )
	{
		BE_TableWriteHeader( t, temp, tempSize, customHeaderColor );
	}

	BE_ListIterate( &t->rowList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		beTableRow_t *row = (beTableRow_t *)BE_IterNext( &iter );

		if ( row->isHorizontalRule )
		{
			BE_TableWriteRule( t, temp, tempSize, row->customHorizontalRuleColor );
		}
		else
		{
			int i;

			for ( i = 0; i <= t->lastColumnIdAssigned && i < TABLE_MAX_COLUMNS; i++ )
			{
				beTableColumn_t *column = (beTableColumn_t *)BE_ListFind( &t->columnList, BE_TableColumnIdMatches, &i );

				if ( !column )
				{
					break;
				}
				BE_TableWriteCell( t, row, column, ( grey && t->alternateColors ) ? qtrue : qfalse, temp, tempSize );
			}
			grey = !grey;
		}
		Q_strcat( temp, tempSize, "^7\n" );
	}

	BE_TableCompressColors( temp, buf, (int)bufSize );
	free( temp );
	Q_strcat( buf, bufSize, "^7\n" );
}
