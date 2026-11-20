# Visual sound indicators (xenjk)

xenjk's `cg_soundindicators` module renders a circular HUD ring around
the centre of the screen showing where recent sound events came from,
what kind of sound they were, and how long ago they fired. It's modelled
on Fortnite's "Visualize Sound Effects" accessibility feature, with
some Star-Wars-specific additions for force powers, sabers and movers.

A second mode overlays the same indicators onto the existing minimap
(only visible in team modes / vehicles, where the radar is normally
drawn).

## Quick start

The feature is parked and off by default (owner 2026-10-07: useful, not finished). In game console:

```
/cg_soundIndicators 1            # toggle centre ring (default 0)
/cg_soundIndicatorScale 1.5      # resize centre ring (0.25 - 4.0)
/cg_soundIndicatorRadar 1        # also show on the radar (default 0)
```

To turn the whole thing off:

```
/cg_soundIndicators 0
/cg_soundIndicatorRadar 0
```

## Sound → icon → colour legend

Categories are deliberately grouped so the icon does most of the work
(à la Fortnite). The colour gives a quick category read at a glance.

| Sound | Icon | Colour | Source events in cgame |
|---|---|---|---|
| Footstep / metal / splash / wade / swim / jump / roll | footprint (`fsrMarkShader`) | white | `EV_FOOTSTEP`, `EV_FOOTSTEP_METAL`, `EV_FOOTSPLASH`, `EV_FOOTWADE`, `EV_SWIM`, `EV_JUMP`, `EV_ROLL` |
| Fire weapon (primary) / alt-fire / saber attack | rocket (`mAutomapRocketIcon`) | red | `EV_FIRE_WEAPON`, `EV_ALT_FIRE`, `EV_SABER_ATTACK` |
| Missile impact / miss / metal-miss | rocket | orange | `EV_MISSILE_HIT`, `EV_MISSILE_MISS`, `EV_MISSILE_MISS_METAL` |
| Item pickup — Weapon | the actual weapon's HUD icon | yellow | `EV_ITEM_PICKUP` (with `giType == IT_WEAPON`) |
| Item pickup — Health (bacta) | `i_icon_bacta` | yellow | `EV_ITEM_PICKUP` (with `IT_HEALTH`) |
| Item pickup — Armor / shield | `i_icon_shieldwall` | yellow | `EV_ITEM_PICKUP` (with `IT_ARMOR`) |
| Item pickup — Holdable (medpack, seeker, sentry, jetpack, cloak, binoculars …) | that holdable's HUD icon | yellow | `EV_ITEM_PICKUP` (with `IT_HOLDABLE`) |
| Door open | useablehint hand (`gfx/hud/useablehint`) | yellow | `EV_PLAYDOORSOUND` |
| Elevator / lift / brushmodel motion | radar arrow (`gfx/menus/radar/arrow_w`) | pale cyan | `EV_BMODEL_SOUND` |
| Force power — drained | force swirl (`forceCoronaShader`) | light blue | `EV_FORCE_DRAINED` |
| Force power — absorb / absorbhit / protect / protecthit / forceJump / forceGrip | force swirl | light blue | `EV_PREDEFSOUND` (any `PDSOUND_*`) |

Colour intent:

- **white** — proximity / movement, low threat
- **red** — outgoing fire from a player
- **orange** — explosion / impact (Fortnite groups red+orange together;
  we keep them split to distinguish "someone is shooting" from
  "something just blew up")
- **yellow** — interactive object / pickup (Fortnite uses gold for
  chests; this mirrors that)
- **light blue** — force power (xenjk-specific, no Fortnite analog)
- **pale cyan** — environmental motion (elevators, doors)

Self-suppression: indicators don't fire for sounds the local player
made. So you don't see your own footsteps as indicators.

Range cull: events further than 3500 world units (Quake units) get
ignored, even if you'd theoretically hear them through walls.

Fade: each indicator lives ~1.5 seconds, fading in over the first
~100 ms and out over the last ~500 ms.

Throttle: same-source same-type events within 60 ms collapse into a
single indicator (refreshes its position) so a continuously-firing
repeater doesn't draw 30 stacked icons.

## Centre-ring vs radar overlay

The two modes are independent. Mix and match:

| `cg_soundIndicators` | `cg_soundIndicatorRadar` | What you get |
|---|---|---|
| 1 | 0 | Just the centre ring (default) |
| 0 | 1 | Just radar icons, no centre ring |
| 1 | 1 | Both — most info, most clutter |
| 1 | 2 | Centre ring + minimal coloured dots on radar |
| 0 | 2 | Compact mode: only tiny dots on radar |

Radar overlay caveat: JKA only draws the radar in **team modes and
when piloting a vehicle**. In FFA/Duel modes there's no radar to
overlay onto, so `cg_soundIndicatorRadar` has no effect there. The
centre ring works everywhere.

Radar overlay clamps off-radar sounds to the rim. So gunfire from
4000+ units away (well past the in-game `cg_radarRange = 2500`) still
produces a red pulse on the rim of the radar pointing in that
direction — useful for big-map situational awareness.

## How modders integrate this into their own OpenJK fork

The module is self-contained in two files plus a single observer hook.
[Detailed integration recipe is at the top of `cg_soundindicators.h`.](../codemp/cgame/cg_soundindicators.h)

Summary of what gets touched in vanilla OpenJK:

| OpenJK file | Lines added | What |
|---|---|---|
| `cg_event.c` | 1 | Single observer call at top of `CG_EntityEvent` |
| `cg_local.h` | 1 | `#include "cg_soundindicators.h"` |
| `cg_main.c` | 1 | `CG_SoundIndicators_Init();` in `CG_Init` |
| `cg_draw.c` | 2 | Centre ring call + optional radar overlay call |
| `cg_xcvar.h` | 3 | Three `XCVAR_DEF` lines for the cvars |
| `CMakeLists.txt` (cgame) | 2 | Add `.c` and `.h` to source list |
| **Total** | **10** | All single-line hooks pointing at our module |

Everything else lives in `codemp/cgame/cg_soundindicators.{c,h}`.

## Adding a custom sound type from a mod

If your mod has events that aren't in the standard JKA event list, you
can flag them on the indicator without modifying `cg_soundindicators.c`:

```c
#include "cg_soundindicators.h"

// Somewhere in your event handler:
CG_SoundIndicators_AddEnt(es->number, SI_TYPE_WEAPON);

// Or with a custom icon:
CG_SoundIndicators_AddWithIcon(es->origin, SI_TYPE_USE, es->number,
                               my_custom_icon_handle);
```

The 6 type values (`SI_TYPE_WEAPON`, `_EXPLOSION`, `_FOOTSTEP`,
`_FORCE`, `_USE`, `_MOVER`) cover the standard categories. If you need
a new category, add an entry to the enum in `cg_soundindicators.h` and
add cases to `SI_GetColor` and `SI_GetIcon` in the `.c` file.

## Cherry-picking just the sound indicators into your own fork

```sh
git remote add xenjk https://github.com/RebirthMeow/xenjk.git
git fetch xenjk
git cherry-pick <sound-indicators-commit-hash>
```

The sound indicator commit is independent — it doesn't depend on
anything else in xenjk (Bot2, Theora video, the POT lift, etc.).
