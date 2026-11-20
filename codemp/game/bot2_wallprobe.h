#ifndef BOT2_WALLPROBE_H
#define BOT2_WALLPROBE_H

// Wall-move probe (bot_moveWallProbe): rolls a fixed sweep of wall-move inputs
// out from a declared pose at one Pmove boundary and prints [WALLPROBE] lines.
// Include g_local.h first. Inert while the cvar is empty.
void Bot2_WallProbe(gentity_t *ent,pmove_t *move);

#endif
