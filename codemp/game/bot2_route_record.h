#ifndef BOT2_ROUTE_RECORD_H
#define BOT2_ROUTE_RECORD_H

void RouteRecord_Init(void);
void RouteRecord_Shutdown(void);
void RouteRecord_Frame(void);
void RouteRecord_Disconnect(int client);
void RouteRecord_Command(int client, const char *action);
void RouteRecord_ArmBot(int client, int releaseLevel);
qboolean RouteRecord_ConsoleCommand(void);
void RouteRecord_Before(gentity_t *ent, const pmove_t *move);
void RouteRecord_AfterPmove(gentity_t *ent, const pmove_t *move);
void RouteRecord_AfterEffects(gentity_t *ent);

#endif
