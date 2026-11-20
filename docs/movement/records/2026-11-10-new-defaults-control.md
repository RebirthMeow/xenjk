# New-defaults control (2026-11-10)

Owner, 2026-11-10: "Do 1 then 2". Item 1 of HANDOFF "Next action (2026-11-10)": a control on the defaults the owner
turned on that day (1fb110d0: `bot_carrierSkHomeMs` 5000, `bot_defenderDropReach` + `bot_defenderReachEdge` 1,
`bot_moveAvoidFull` 1, with the latent and lossless fixes), which later arms pair against, and the check HANDOFF asked
for on Kejim captures and Dantooine runners' falls.

## Setup

- Build `review-next-build-20261110-g` (1fb110d0), arm `nd` = its defaults.
- Seeds the guide campaign's old-default control ran: Kejim 35-82 (48), Dantooine 35-50 (16). Paired by seed against
  `guidelong-20261109-a/worlds/ctl-*` (build -a, the defaults before 1fb110d0).
- The milestone-B 4v4 (`laps_campaign.COMMON` + `--two-sided`), 600 game s at rate 25, frame meter on, four worlds at
  a time. Not timed.
- Evidence: `.codex-research/ndctl-20261110-a/` (runner `runarms.py`, `runarms.log`, `compare-kej.txt`,
  `compare-dan.txt`, `worlds/nd-*`). 64 worlds, none failed, all distinct.

## Results (new defaults minus old, per 600 game s)

| | Kejim (48 pairs) | Dantooine (16 pairs) |
|---|---|---|
| team captures | 3.88 → 4.08, +0.20 ± 0.22, undecided | 4.12 → 4.38, +0.26 ± 0.31, undecided |
| runner falls | +0.09 ± 0.40 | +0.00 ± 0.40 |
| runners' falls (both teams) | +0.15 ± 0.50 (inside ±15%) | 2.77 → 3.70, +0.93 ± 0.56 |
| defender holding the enemy flag | +0.16 ± 0.13 | +0.00 ± 0.27 |
| work meter: est frames over 25 ms | 584 → 584 | 1,965 → 1,708 (-13%) |
| guide cells settled a world | 11.8M → 12.0M | 13.4M → 13.5M |
| largest frame's guide cells (mean of worlds) | 54,339 → 52,350 | 69,295 → 64,486 |

- Nothing passes the effect cut. Captures read slightly up on both maps, as BA-2/9's arm did (Kejim +0.68 ± 0.32 on
  16 pairs); with 48 pairs the Kejim reading is smaller.
- Dantooine runners' falls lean up (+0.93 ± 0.56, 1.7 se), the item HANDOFF asked to watch; BA-2/9's own arm read
  them down on Kejim (-0.99 ± 0.80). Not decided; a later Dantooine campaign can pool these 16 pairs.
- Dantooine's work-meter tail drops 13%, the standoff fix (`bot_moveAvoidFull`), as its record found in one world.
- Wall time per world (four at once, not a timing measure) was 157 s on Kejim and 194 s on Dantooine against 139 s and
  164 s for the old control the day before.

## Use

`ndctl-20261110-a/worlds/nd-*` is the control for arms on builds that replay it (checked per build with
`laps_identity.py`); the spreading arm ([2026-11-10-guide-spread.md](2026-11-10-guide-spread.md)) is the first.
