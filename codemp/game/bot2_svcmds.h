// ==============================================================================
// bot2_svcmds.h - Bot2 server (rcon / dedicated console) commands.
//
// Commands that drive Bot2 from the server console, referenced by name from the
// g_svcmds.c svcmd table; the bodies live outside stock OpenJK source.
// ==============================================================================
#ifndef __BOT2_SVCMDS_H__
#define __BOT2_SVCMDS_H__

void Svcmd_AddRunner_f( void );
void Svcmd_BotPlayRate_f( void );
void Svcmd_BotLevelTime_f( void );
void Svcmd_BotAtRan_f( void );
void Svcmd_BotAt_f( void );
void Svcmd_BotKill_f( void );
void Bot2_AtFrame( void );
void Bot2_PlayLogSample(int clientNum,const usercmd_t *cmd);
void Bot2_PlayLogStop(void);
// addrunner's archived settings put back at a game's final shutdown (not a map_restart).
void Bot2_PlayRestoreArchived(int restart);
void Svcmd_MoveGraph_f( void );   // bot2_graph.c
void Svcmd_AddDefender_f( void ); // bot2_defender.c
void Svcmd_DefenderPost_f( void ); // bot2_defender.c

#endif
