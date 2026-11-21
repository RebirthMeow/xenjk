# Director: shot files for filming in the engine

`director` plays a shot file: a camera on a keyframed path with a lens, and client-only actors with animations and
somewhere to look. It is display only (no collision, no server entity) and runs on the game clock, so a demo played
back with the same shot file gives the same frames every time. Code: `codemp/cgame/cg_director.c`.

It needs `sv_cheats 1` or demo playback, like `benchcam`.

## Shot file

`director/<name>.txt` in the game folder. One statement a line, `//` comments, times in seconds from the shot's start.

| Statement | Meaning |
|---|---|
| `cam <t> <x> <y> <z> <yaw> <pitch> <roll> <fov> [cut]` | A camera key. `fov 0` keeps the game's own. `cut` jumps to this key instead of gliding into it. |
| `interp smooth\|linear` | How the camera moves between keys (default smooth). |
| `at <t> cam look point <x> <y> <z> \| actor <id> [part] \| client <n> [part] \| off` | Aim the camera at something. `part` is `head` (default), `chest`, `origin`, or a height above the body's origin. Head and chest follow the bones, so they are right whether the body stands, sits or crouches. |
| `at <t> cam rel client <n> \| actor <id> \| off` | Camera keys become offsets from that body: x ahead, y to its left, z up; its yaw is added. For tracking shots. |
| `actor <id> <model> [skin <name>] [scale <f>] [saber <colour>]` | Loads `models/players/<model>/model.glm`. Declare an actor before its `at` lines. `saber red\|orange\|yellow\|green\|blue\|purple` puts the stock hilt in its right hand with a lit blade (the model needs a `*r_hand` tag). |
| `at <t> <id> saber on\|off` | The blade grows or shrinks over 0.3 s. It is on unless told otherwise. |
| `light <x> <y> <z> <radius> <r> <g> <b>` | A light at a point for the whole shot. Also `light actor <id> <ahead> <left> <up> <radius> <r> <g> <b>` (carried with that actor) and `light cam <ahead> <left> <up> ...` (carried with the camera). Lengths are in the frame's units. Colours near 0.2 light a character 3 m away; 1.0 burns it white. Up to 16. |
| `frame <ox> <oy> <oz> <scale>` \| `frame off` | Positions on the lines after it are in another frame: game = (given - o) * scale. For writing a shot in a map's source units. |
| `at <t> <id> pos <x> <y> <z> <yaw> [drop]` | Put the actor there. The position is a player origin, as `viewpos` prints it. `drop` stands it on the floor under that point: the highest one not more than a step above it, so a point written inside a canopy or a slab still lands on the floor below. |
| `at <t> <id> face point <x> <y> <z> \| cam \| actor <id> \| client <n> \| off` | Turn the whole body toward it, instead of working out a yaw. |
| `at <t> <id> anim <NAME> [loop\|hold] [blend <ms>] [speed <f>]` | An `animation.cfg` row on the whole body, for example `BOTH_SIT2`. |
| `at <t> <id> frames <first> <last> [loop\|hold] [blend <ms>] [speed <f>]` | A raw frame range of the model's own skeleton file. |
| `at <t> <id> move <x> <y> <z> <seconds> [drop]` | Glide there, facing the way it goes. Pair it with a walk animation. |
| `at <t> <id> look point <x> <y> <z> \| cam \| actor <id> [part] \| client <n> [part] \| off` | Turn the head and neck (up to 65 degrees to a side, 25 one way and 55 the other vertically). |
| `at <t> <id> hide \| show` | |
| `player hide` | Leave the local player's own body out of the shot (the player still has to stand nearby on maps that load collision around them). |
| `length <t>` | The shot ends here (default: the last key or event). |
| `start <console command>` | Run when the shot starts, for example `video shot01`. |
| `end <console command>` | Run when the shot ends, for example `stopvideo`. Several commands can be joined with `;`. |

An actor is not drawn until its first `pos`. The last pose (camera and actors) stays up after the end until
`director stop`.

## Commands

| Command | Meaning |
|---|---|
| `director play <name> [from]` | Load and start now, optionally from a time in seconds. |
| `director arm <name> <clock ms>` | Load, show the first pose, and start when the game clock reaches that value. In a demo that is the same moment on every playback. |
| `director show <name> <t>` | Hold the shot still at one time, for a screenshot. Repeating it with another time keeps the loaded models. |
| `director check <name> [step]` | Trace the shot against the map and print what is wrong, before anything is rendered (see below). |
| `director floors <x> <y> [z]` | Every floor someone could stand on at that x y (game units), top first, from 1,024 above z (default: the player's height) down. |
| `director radar <x> <y> <z> [drop]` | How far a camera can back away from that point before it meets a wall: straight up, then every 15 degrees of yaw. `drop`: from eye height above the floor under the point. `radar here 0 0`: from the player. |
| `director time` | Print the game clock and where the shot is. |
| `director seek <t>`, `director pause`, `director stop` | |
| `director key [t]` | Append a `cam` line for the current view to `director/keys.txt`. |
| `director here <id> <model> [ANIM]` | Append an actor standing where the player is to `director/keys.txt`. |

## Filming a shot

1. Fly to each camera position and run `director key`; stand where each actor goes and run `director here`. Copy the
   lines from `director/keys.txt` into the shot file and set the times.
2. For anything a real player does (walking through, riding), record a demo, and note `director time` at a moment in it.
3. Play the demo and arm the shot on that clock:

   ```
   demo myride
   wait 200
   director arm shot01 21500
   ```

   with `start video shot01` and `end stopvideo` in the shot file. `video` records demo playback at a fixed frame step
   (`cl_aviFrameRate`), so every frame lands at the same shot time on each run.

The start and end commands go to the back of the console's queue. A script that plays a shot must not leave `wait`
commands queued behind it, or those run first and the recording never starts.

## Checking a shot before filming it

`director check <name>` loads the shot and traces it against the map's collision. It reports:

- **Camera:** the times it is inside or within 6 units of a wall, passes through a surface, cannot see what it is aimed
  at, or jumps without a `cut` (a body it is `rel` to turned or moved).
- **Actors:** for every `pos` and `move`, where the body ends up and every floor at that spot (the one taken is
  starred); a body floating, sunk or inside something solid; someone nearby standing a storey higher or lower (a canopy
  or a roof taken for the street); a `move` that is blocked, with its speed beside a walk's and a run's.
- **Heads:** how far each `look` turns the head, and the ones past what the neck allows.

Positions are printed in the file's own units (its last `frame`), so they paste back into the shot. The check needs the
map's collision at those places: on a map that loads collision only near the player, stand the player by the shot
first. It reads collision, not what is drawn: a prop without collision does not show up, and neither does a wall that
only exists in the picture. Run it, fix what it prints, then look at stills.

`director show <name> <t>` followed by `screenshot` gives one still. A script can step through several times in one
launch and tile the stills into a contact sheet, which shows framing and poses in under a minute without a demo.

Drops are taken when the shot starts (or is shown or checked), not when the file is read. `drop` needs the map's collision under that point. On a map that loads collision only near the player, an actor far
away finds no ground: the shot prints a warning and keeps the height given.

A camera `rel` to a body follows that body's yaw: a `face` on it swings the camera. Keep `rel` for bodies that do not
turn in the shot, or drop it before the turn.

## Limits

- Whole-body animations only: no separate torso and legs. An actor's saber is one stock hilt with a blade and its
  glow: no trail, no clash, no sound. A character who must ride should be a real player in the demo.
- Camera keys under `rel` are offsets in game units: write them after `frame off`.
- A game command a script triggers (`addrunner` queues an `addbot`) runs after everything the script still has
  queued behind its `wait`s. Issue such commands in the script itself.
- Heads turn; faces do not move.
- `move` is a straight glide at constant speed, so feet slide unless the speed matches the walk.
- Up to 48 actors, 512 camera keys and 2,048 events a shot.
- A demo only holds what was near the recording player. On maps with visibility data, a camera far from that player may
  see entities missing.

## Many shots in one playback

A shot's `end` line can arm the next one: `end stopvideo; director arm shot02 61500`. With `start video shot01` in each
file, one demo playback records a whole list of shots, each to its own file. Record the demo with the player moved to
each shot's place in turn (noclip), printing `director time` after each move, and arm each shot a few seconds after its
move so the place has loaded.
