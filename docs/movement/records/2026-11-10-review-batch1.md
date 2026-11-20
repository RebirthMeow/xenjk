# Review batch 1: fixes that leave bot-only worlds unchanged (2026-11-10)

Owner, 2026-11-10: "Yes" to batch 1 of the 2026-11-09 review's remaining open findings. These are the ones the
lane reviews mark "none", "latent" or "log only" for outcomes, so a seeded bot-only world must replay. Commit ce7f0cd9
on `claude/bot2-review-next`; the defender and movement parts were written by two subagents and reviewed here.

## Done

| Finding | Change |
|---|---|
| BP-8 | The forecast guard scope copies and compares `level.num_entities` slots, not 1,024; the level struct (and `num_entities` with it) is compared first. The rollout copy covers `max(num_entities, high-water)` slots, so a restart's stale tail is still overwritten (`bot2_forecast.c`). |
| BP-15 | The guidance header now says the clearance penalty is in the tables (comment only). The node and merge-tool parts need a version bump and stay open. |
| BP-16 | `PathMoverStanding` keeps the list of lift cells per graph (keyed on cell count and `guideTopo`) instead of scanning every cell per query. `Graph_BodiesEncloseGoal`'s scan is left as it is. |
| BX-6 | `Graph_EdgeFail` moves `reachTopology` only when the edge goes off or is disabled; the reach sets read nothing else. |
| BX-7 | A straight judge whose guard was contaminated (this scope or a resumed one) ends as `guard_busy` with no charge; a deferred drop search that stopped at contamination is a stale result (`reason=deferred_contaminated`). The subagent's first version read `guard.contaminated` before `Bot2_ForecastGuardEnd`, which only End sets, and so did nothing; fixed before the build. |
| BM-16 | A teleport-bit change with a committed sequence or a ride ends it as a knock does (no charge), resets parity and asks for a replan. A respawn flips the bit too, but the plan and ride are already cleared by then. |
| BM-17 | `bot_defenderStrafe` (the step's guest test), `bot_moveGuestBrake`, `bot_moveEdgeHull` and `bot_moveLandingJump` are latched in `FlightModeRead`, and lazily once per `level.time` for a caller that reaches the step first, with today's parsing. |
| BA-8 | `Escapes` stops at the first non-lethal direction in mode 2, where only nonzero is tested; `Shadow` and the patience check keep the full count. |
| BA-15 | `TargetCandidate` accepts any connected enemy on red or blue, humans included. |
| BA-19 | The stranded timer is cleared when a slot gets a new occupant (`BotAISetupClient`), not on mid-life `Bot2_ClearState` calls. |
| BA-22 (first bullet) | The threat scan stops at `level.num_entities`. |
| BA-24 | A defender re-reads its stands after a team change (`[DEFENDER] STANDS`). |

## Not done

- **BP-7** (the deferred save copies the whole edge array): about 30 sites write edge records, with no single
  mutation point, so a dirty range needs a full audit of every mutator first. Open.
- **BA-10** (the camper decides after its self-kill): the subagent found that `Engage` on the dead body also sets
  weapon, aim and throw state that the respawn does not reset. With `bot_carrierSkHomeMs` 5000, campers self-kill in
  the test worlds, so the fix can change a world. Moved to the outcome batch.

## Check

Build `review-next-build-20261110-i` (ce7f0cd9) with its defaults, Kejim and Dantooine seeds 35 and 36, against the
new-defaults control `ndctl-20261110-a/worlds/nd-*` (build -g), using the branch's `tools/laps_identity.py`:
- **Replay:** all four worlds identical (game log and every decision line).
- **Work:** `[FRAMEWORK]` traces fall 0.2-1.1% on three of the four (Kejim 35: 76.67M → 76.54M; Kejim 36: 84.20M →
  83.30M; Dantooine 35: 103.40M → 103.06M), probably BA-8's early-out; Dantooine 36 is unchanged.
- **Evidence:** `.codex-research/batch1-20261110-a/` (`run.sh`, `run.log`, `identity-*.txt`, `worlds/`).

Not timed.
