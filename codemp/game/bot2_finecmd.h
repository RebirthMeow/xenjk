#ifndef BOT2_FINECMD_H
#define BOT2_FINECMD_H

// Bots' Pmove: one command per server frame, re-issued every pmove_msec while
// climbing a wall or entering a climb (bot2_finecmd.c). Include g_local.h first.
qboolean Bot2_FineCommand(const pmove_t *pm);
void Bot2_Pmove(pmove_t *pm);

#endif
