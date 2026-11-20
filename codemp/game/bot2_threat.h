// Reaction time to moving threats (bot2_threat.c): young missiles are unseen while a bot decides.
#ifndef BOT2_THREAT_H
#define BOT2_THREAT_H

void Bot2_ThreatHideBegin(void);
void Bot2_ThreatHideEnd(void);
// Every other player, NPC and missile unlinked, for a counterfactual search:
// would this bot have found a way with no transient body in the world?
// Relinked by End before anything else runs. Not nested.
void Bot2_TransientHideBegin(int self);
void Bot2_TransientHideEnd(void);

#endif
