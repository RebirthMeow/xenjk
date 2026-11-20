# 2026-10-14: client checks (prediction, drain icon, spectator timers, CTF stats)

Queue: [overnight-tests.md](../../overnight-tests.md) "Client checks" (owner 2026-10-13: agents do them). Installed
build (Install 13: `GameData\xenjk.x86.exe`, rd-vulkan; game and cgame modules in `GameData\base`, no game-side change
since they were installed), hidden and muted, private homes, localhost only. Harness and every run:
`.codex-research/client-checks-20261014-a/` (README there).

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| Prediction, landing from a high force jump | no misses or errors (3 straight up, 285 u rise; 3 running, 243 u; 2 more in s5) | s4, s5 |
| Prediction, rolling | no misses; `BOTH_ROLL_F` x2, `_R`, `_L`, `_B` by `cg_debugAnim` | s5 |
| Prediction, wall grab after a fall without a jump | no misses; `BOTH_FORCEWALLREBOUND_FORWARD` twice (z holds on the wall, then rises) | s6, wallx |
| Prediction, standing on a player's head | no misses; A's eye 64 u over B's, 1 u apart, stays there and lands back after a hop | s6 |
| Drained player's debuff icon | shown on the drained player's HUD with a countdown (4 -> 2), red shell on the model | s6 screenshots `drain_a1..after2` |
| Force timers while spectating | "Following ClientB", Speed and Rage icons counting down (19/20 -> 15/15) | s6 screenshots `spec_t1..t4` |
| CTF stats to intermission | tables print by themselves at the capture-limit intermission (general, force, damage matrix, accuracy, weapons); `stats wpn all` and `stats acc` mid-match and at intermission | s7b, s7e |
| Accuracy with tracked weapons | ClientA 66% (pistol, disruptor, repeater); damage dealt 20 / 78 / 42 | s7e |
| A player who switches team keeps rows | yes: ClientB's blue rows (164 damage taken in s7b) stay in every later table and a red row is added beside them (one row per team) | s7b, s7e |

Every `PredictionTeleport` line matches one of the script's own `setviewpos` teleports. One `prediction error` plus
`Prediction miss: 82.8` prints in every session at the moment the client first joins a team (spawn from the
spectator view); it is outside the listed scenarios and was not investigated.

## Limits

- The wall grab is from a fall that did not start with a jump (on the ground, then placed in the air beside the wall),
  which is the state a ledge drop leaves (`forceJumpZStart` 0, the `g_fixWallgrab` path). A scripted walk-off cannot
  press jump within the few frames the player is still within 8 u of the face; s5's attempts never left the ledge.
  The two sideways (`+moveright`) attempts did not grab.
- Rolls and wall moves need the saber or melee (bg_pmove.c), which force the third-person camera, so they are judged
  by animation names and screenshots, not positions.
- ClientB's return fire never hit A in s7e (B's accuracy row reads 0).

## Lessons for scripted client runs

Tooling ideas from these: [client-test-tooling-notes.md](../../client-test-tooling-notes.md).

- `setviewpos` (TeleportPlayer) pushes 400 u/s along the yaw for 160 ms (about 190 u on the ground) and telefrags
  anything the destination box overlaps, `god` or not; a wall press needs the jump at least 0.45 s after the teleport.
- `viewpos` prints the camera: with the saber or melee out it is the third-person camera. Hold a gun (`weapon 3` is the
  blaster; `weapon 1` is the saber and toggles it off if already out) for positions.
- Movement graph heights are not game heights: graph z + 52 = eye z on Kejim, and the graph misses walkways.
- On a listen server the match-end line ("Exit: ...") goes only to games.log; a 0-0 CTF tie at the timelimit goes to
  sudden death.
- The installed `openjk.cfg` in GameData/base is read by a private home too (the owner's model, saber, binds,
  `cg_thirdPersonRange`); nothing is written back to GameData.
