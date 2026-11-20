// The Pmove boundary wrapper and the fixtures' pre-start hold (bot_movebench_hold).
#ifndef BOT2_MOVEBENCH_H
#define BOT2_MOVEBENCH_H
qboolean Bot2_MoveBenchRunClient(gentity_t *ent);
void Bot2_MoveBenchPmove(gentity_t *ent, pmove_t *move);
#endif
