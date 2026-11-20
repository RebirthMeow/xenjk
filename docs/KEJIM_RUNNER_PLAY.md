# Play against the movement runner

`tools/play_vs_runner.py` runs a private CTF match against `Runner_v2`, the
movement runner. It uses a local server on 127.0.0.1, puts you on one team
and the runner on the other, and runs the runner with the settings the
movement gates measure. Nothing in the installed game directory is written.

This guide replaces the 10 September playtest instructions. Those
instructions described the old runner, which is now frozen with the rest
of the [pre-rewrite archive](archive/strafe-movement-2026-09-14/README.md).

## What you need

- A Jedi Academy install at `game_directory/Jedi Academy/GameData`, with
  the xenjk client built into it (`build_and_install.bat`).
- A fixture build of the dedicated server and game DLL, made with
  `tools/strafe_fixture_build.py`. The launcher checks the build's hashes
  before it starts.
- Map data under `.codex-research/`, which is not in Git:
  - on Kejim, the production graph seed and decision library;
  - on other maps, a prepared seed made by `tools/movement_graph_prepare.py`
    ([map preparation](movement/PLAN.md)).

  A map without prepared data still plays. The runner builds its graph when
  the map loads, but its routes are slower.

## Start and stop

```
python tools/play_vs_runner.py --build .codex-research/<build>
python tools/play_vs_runner.py --build .codex-research/<build> --map mp/ctf_dantooine_v3 --team blue
python tools/play_vs_runner.py --stop .codex-research/play-vs-runner-<stamp>
```

| Option | Effect |
|---|---|
| `--map` | Any map on the [frozen list](movement/PLAN.md); default `mp/ctf_kejim` |
| `--team` | Your team; the runner joins the other; default red |
| `--set CVAR=VALUE` | Extra server settings, repeatable |
| `--no-client` | Server and runner only: the runner laps alone |
| `--home` | The server's writable files; default `%LOCALAPPDATA%\xenjk-play` |

`--stop` shuts the server down cleanly and copies the session into
`.codex-research/play-vs-runner-<stamp>/live`: the server console log with
every `[MOVE]` line, the runner's play log and the server's demos.

Keep `--home` on a fast drive. On a busy hard disk the server's small writes
can block the game thread for about a second at a time, which shows up in
game as ping 999.

## In the match

Run these as rcon or server console commands:

- `adddefender <team>`, then `defenderpost`, adds the scripted defender
  from the pilot, which pushes and pulls at pit edges. Remove it with
  `kick Defender_v2` ([fixture notes](../tools/fixtures/movement/defender/README.md)).
- `botplayrate 2.5` speeds the whole match up; `botplayrate 1` restores
  normal speed.

When an opponent pins the runner while it carries the flag, it fights back
with its staff (`bot_carrierMelee`). Otherwise it does not fight.

## Localhost only

These tools start their own server on 127.0.0.1. Never point them at a
public server.
