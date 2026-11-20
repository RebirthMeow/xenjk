// ==============================================================================
// bot2_kick.h - declared knockback fixture (bot_moveKick). See bot2_kick.c.
// ==============================================================================
#ifndef __BOT2_KICK_H__
#define __BOT2_KICK_H__

void Bot2_KickArm(int client,int releaseLevel);   // at bot_corner_start
void Bot2_KickFrame(int client);                  // once per command, before the decision

#endif
