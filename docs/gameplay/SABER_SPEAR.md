# Spear saber throw

`g_saberSpear 1` replaces the spinning saber throw with a spear throw: point first, fast, a slight drop, not
steered and not held out. A kill with a wall close behind nails the body to the wall, as Painkiller's stake gun
does. `g_saberSpear 0` (the default) is the stock throw, unchanged.

| Part | Path |
|---|---|
| Server: flight, hits, carry, pin, release | `codemp/game/g_saberspear.c` |
| Launch hook | `codemp/game/w_saber.c` (thrown-saber setup in `WP_SaberPositionUpdate`) |
| Pinned client corpse holds still | `codemp/game/g_active.c` (`SaberSpear_ClientPinned`) |
| A respawn's body copy keeps the pin | `codemp/game/g_client.c` (`CopyToBodyQue` → `SaberSpear_BodyCopied`) |
| Client: spear angles, ragdoll pin | `codemp/cgame/cg_players.c` |
| Pinned parts and the measured skeleton, shared | `codemp/game/bg_saberspear.h` |

## Behaviour

1. **Flight.** The saber leaves the hand at `g_saberSpearSpeed` (1400 u/s), blade along the flight, dropping at
   0.15 of `g_gravity`. Releasing alt-attack does not recall it. It returns when it runs out of range (700 × throw
   rank) or after 6 s, as the stock throw does.
2. **Hit.** One hit for `g_saberSpearDamage` (100) before the game's usual scaling: the attacker's max health over
   100, then the hit location (`g_locationBasedDamage`; a leg hit measured 70%, so a level throw at waist height
   does not always kill a full-health player). A block works as for the stock throw: the saber is knocked out of
   the air or goes back to its owner. A hit that does not kill, or a kill with no wall behind, sends the saber back.
3. **Pin.** On a kill, a steep world wall (normal z under 0.7, not sky, not a mover) within `g_saberSpearPinRange`
   (128) of the hit starts the carry. The wall is traced at the height of the part the blade went through, and the
   blade stays in that part: chest, head, waist, arm, hand or leg. The body hangs from that part for
   `g_saberSpearPinMs` (1500 ms). For the upper body the spear lifts the part on its way to the wall, up to 32
   units, until the hanging feet clear the floor by 14. A leg is not lifted, so the leg stays pinned and the body
   slumps on the floor. When the pin ends the saber pulls out and returns, and the body drops a little away from the
   wall.
4. **No victim.** A throw that hits a world wall sticks for 400 ms, then returns.

## How the pin works

**Which part.** The flight trace hits only the victim's box, so on the hit the spear casts a ray along its flight
against the model. The surface it meets gives a hit location, and the nearest of that limb's ragdoll bones becomes the
pinned part. Damage takes its hit location from the same surface. The part list and the measured skeleton offsets
are in `codemp/game/bg_saberspear.h`. The part is sent in the saber's `s.generic1`.

**Hanging.** The ragdoll runs on each client and follows the corpse entity's origin. Its root, the pelvis, is held 19
units below the origin. Effector goals cannot lift the body, so the server places the origin where the pelvis hangs
below the pinned part:

- chest: 16 below
- head: 27 below
- arm: 34 below
- hand: 38 below
- waist: at the pin

The body is moved up or down first, then along the wall, then into it, so touching the wall stops neither the height
change nor the slide under the pin. Dead NPCs run no pmove; a pinned client corpse gets `PM_FREEZE`.

The corpse's `ragAttach` names the saber, as the stock corpse drag uses it. The saber carries the pin point in
`s.origin2` and a spear flag in `s.userInt1`. cgame holds the pinned part's bone on the pin point. It hangs the chest,
head, arms and legs straight below the pelvis, each limb on its own side; for an arm or a hand, the body hangs beside
that arm.

The owner's in-flight blade damage pass skips a spear. The blade points ahead and could otherwise kill before the
spear's own hit, and then nothing would be pinned.

A victim can respawn while pinned (bots and players may respawn about 1 s after death). Its body-queue copy then
takes over the pin: the copy holds still at the same place until the saber pulls out, then falls. A body copy has no
box, so on release it gets a dead player's box; without one it landed with its origin on the floor and the ragdoll sank
through the floor.

## Checks

`developer 1` prints one `SPEAR` line per launch, hit (health before and after), carry, pin, stick and return.

Two scripted client checks were run on 2026-11-25. Both used the installed 64-bit client with a private home holding
the built game DLLs, in the `xenjk_showcase` Cornell box by the red wall. The scripts are in the local research folder
`saber-spear-20261125`.

- **NPC victim** (`spear_check.py`): a frozen stormtrooper NPC, thrown at by the local player.
- **Bot victim** (`spear_botcheck.py`): two stormtrooper bots as saber-lab fighters. Fighter a throws
  (`a.act=throw`), fighter b stands holstered, and the local player watches from the side. Spear damage was raised to
  250 so the hit always kills.

Results (part pinning re-checked on 2026-11-25 with NPC victims; the throw pitch and side offset chose the part):

- The flight is point first.
- **Chest:** the body hangs upright with the blade through the chest.
- **Head:** it hangs from the head.
- **Hand:** a level throw that met the right hand pinned the hand; the body hangs from the raised arm, with the feet
  near the floor (the 32-unit lift cap).
- **Knee:** a low throw pinned the knee; the leg stays on the wall and the body slumps on the floor.
- **Bot victim:** hung from the waist and the chest. When it respawned mid-pin (`g_saberSpearPinMs 4000`), its body
  copy held the full 4 s, then dropped onto the floor.
- A hit that does not kill returns the saber.
- `g_saberSpear 0` throws the stock spinning saber (checked before this change; the stock path is untouched apart from
  the blade-damage skip, which applies only while the spear flag is set).
- A stormtrooper bot whose AI was handed back threw spears of its own; one missed and stuck in the wall for 0.4 s.

Not checked: a human player as the victim (the same client-corpse path as a bot), a diagonal wall hit, a pinned upper
arm (the test throws met the hand, the chest or the waist), a left-side pin, and other players watching over a
network. The legs bend a little at the knees while hanging: the ragdoll's goals are not reached exactly.

Bot AI (`ai_main.c`) still throws as it did: it holds alt-attack, which the spear ignores. Bot2 does not throw.
