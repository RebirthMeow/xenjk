#pragma once

#include "g_local.h"

// Playback of a recorded route tape. Research only: nothing arms itself, and
// an unarmed module costs one comparison per client frame.
qboolean Bot2_RoutePlayClient(gentity_t *ent);
qboolean Bot2_RoutePlayConsoleCommand(void);
void Bot2_RoutePlayShutdown(void);
