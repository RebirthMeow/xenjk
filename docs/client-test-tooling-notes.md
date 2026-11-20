# Client test tooling: what was hard (2026-10-14)

Notes for building better scripted client tests later. Source: the client checks of 2026-10-14
([record](movement/records/2026-10-14-client-checks.md)), which took about 20 runs to get 9 results. The harness
used is `.codex-research/client-checks-20261014-a/cc.py`. Each item below is a problem we hit, then a tool that
would remove it.

## Placing players

- **`setviewpos` is a teleporter, not a placement.** It pushes 400 u/s along the yaw for 160 ms (about 190 u on the
  ground; in the air the speed is kept until landing) and telefrags anything the destination overlaps, even with
  `god`. Every placement had to be planned around the slide; "stand on B's head" needed a wall-press trick (A jumps,
  B teleports under A into the same wall), and two pit deaths came from slides.
  *Tool:* a cheat `setpos x y z yaw [pitch]` (or `setviewpos ... nopush`) that places exactly, zero velocity, no
  KillBox (or KillBox off by flag), and one to place another client (`setposclient <n> ...`) from the server.
- **We did not know where anything was.** The movement graph's heights are a different frame from the game's (Kejim:
  eye = graph + 52), it misses walkways, and an "edge" in it can be a pit rather than a wall. Positions had to be
  found by probing runs in eight directions and reading `viewpos`.
  *Tool:* a per-map fixture of named test spots (open floor, a wall to press, a ledge with a solid face below, a pit,
  flag stands), checked in like `tools/fixtures/movement/`, plus a `nearestwall` / `trace` cheat that prints what is
  ahead.
- **No ledge-drop scenario is scriptable.** A walk-off then a wall grab needs the jump within a few frames of
  leaving the edge (within 8 u of the face); cfg `wait`s and the step loop are too coarse, so we used the equivalent
  state (in the air, no jump since the ground).
  *Tool:* frame-exact input (below), or a fixture spot with a slow edge (a slope or a crouch-walk ledge).

## Driving input

- **cfg scripts are open-loop and coarse.** `wait` counts frames (about 4.6 ms each here, measured per run), the
  runner can only react between steps (polled every 25 waits), and nothing can branch on game state ("press jump when
  airborne").
  *Tool:* a usercmd script player in the client: a file of per-frame commands (buttons, moves, angles) with
  conditions (`until airborne`, `until z < n`, `at serverTime`), or a demo-style input track recorded from a person
  and replayed. Pitch control too (setviewpos sets yaw only; there is no `setangles`).
- **Two clients had to be sequenced through files.** Each polls `exec <name>_<n>`, the runner writes the next step
  when it sees the previous marker. It works but costs 0.1-0.2 s per step and spams "couldn't exec" lines.
  *Tool:* a server-driven test script (like `botat`) that issues commands to any client by number at game times, so
  one file describes the whole multi-client scenario on the game clock.
- **Weapon slots are by key, not weapon number.** `weapon 1` is the saber and toggles it off when already out;
  `weapon 3` is the blaster. Cost two runs.
  *Tool:* `setweapon <WP_ name>` that selects exactly.

## Observing results

- **`viewpos` prints the camera, not the player.** With the saber or melee out (needed for rolls and wall moves) the
  camera is forced to third person, so positions were wrong by the camera range until we switched to a gun.
  *Tool:* a `playerstate` dump command: origin, velocity, groundEntityNum, legs/torso anim, pm_flags, forceJumpZStart,
  health, force power, weapon, as one parseable line, and optionally every frame for a window.
- **Proof that a move happened had to be inferred.** Rolls looked like crouch-walks by speed; wall grabs were read off
  a z curve (flat hold, then rise) until `cg_debugAnim -1` named the animations.
  *Tool:* the same state dump with anim names; assertions in the test file ("expect BOTH_ROLL_* within 0.3 s").
- **Prediction misses need attribution.** `cg_showmiss 1` prints lines without a scenario; we split them by echo
  markers and had to subtract the script's own teleports (`PredictionTeleport`). One unexplained miss (82.8 u at the
  first team join) still prints in every session.
  *Tool:* a prediction-miss counter with reasons (teleport, event, real miss) and the server frame, reset and read per
  scenario.
- **Screenshots were the only check for HUD items** (drain icon, spectator timers), judged by eye.
  *Tool:* cgame prints of HUD state (which icons and timers are drawn), or a screenshot crop compared with a
  reference image.
- **Match end is quiet on a listen server.** "Exit: ..." goes only to games.log, and a 0-0 CTF tie at the timelimit
  goes to sudden death, so the first stats run never reached intermission.
  *Tool:* the runner reads games.log by default; fixture settings that always end (capturelimit 1 plus a scripted
  capture, or a `endmatch` cheat).

## Environment

- **Owner settings leak into private homes.** A private `fs_homepath` still reads `GameData/base/openjk.cfg` (model,
  saber, third-person range, binds), which changed camera offsets.
  *Tool:* a `+set fs_skipOwnerCfg 1` style switch or a clean cfg the runner execs first.
- **Failures were silent.** A script error left a client running; a dead player kept receiving commands for a whole
  session (s1, s5).
  *Tool:* the runner checks health/team after each step and stops on death unless expected; always closes clients.

## Priority (my suggestion)

1. A state dump command (position, velocity, anims, flags) and an exact `setpos` without push or telefrag: these two
   would have removed most of the 20 runs.
2. Named test spots per map.
3. A server-driven, game-clock scenario file for several clients, with per-frame input and conditions.
4. Prediction-miss counter with reasons; HUD state prints.
