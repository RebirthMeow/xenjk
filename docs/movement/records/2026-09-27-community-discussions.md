# Community discussions: what the CTF community says, checked against demos, 2026-09-27

Part of the demo position pilot ([record](2026-09-27-position-pilot.md)).
Source: an export of the community's public Discord channels (the `#ctf-discussion` channel, 11,279 messages, April
2020 to 2026; the forum threads "Offense Survival Meta/Protect" (1,691,
November 2023 to June 2024), "CTF Dictionary", "Drain", "Golan", "Grip";
the Kothis-changes thread). Converted to text and read in eight parts;
per-part notes in `.codex-research/position-pilot-20260927/discussions/`
(`notes-*.md`). Claims are the community's until a demo check below says
otherwise; each is marked by how it was stated there. Dates matter: several
rules changed during the period, so an older claim can be true of older
demos only.

## Vocabulary (CTF Dictionary thread and usage)

- **Holding** an enemy flag takes two forms: **circle-running** (a looping
  route chosen to make the chase jump pits or climb; Kejim, Illimiran,
  Metalorn) and **protect-spamming** at a protect-friendly spot away from
  pits (Dantooine hut, Illimiran, Kharzet lift, Manaan). Good carriers mix
  both moment to moment.
- **Camping**: the non-carrying offense stays near the enemy stand so the
  enemy carrier cannot cap if its flag is returned. Whether "camping" also
  covers helping the chase is disputed; **gerring** is leaving one's
  position to chase the enemy carrier (once derogatory, now partly
  accepted). **Forcing a cap**: late and behind, the camper plays second
  chase or base. **Guarding**: after killing the enemy carrier, not
  returning their flag, to time one's own cap.
- `sk` self-kill, `TE` team energize, `TH` team heal, `ret` return.

## Mechanics the community states

Agreed by several unless marked; server is base_enhanced with the Newmod
client.

| Item | Statement |
| --- | --- |
| Push, pull | 20 Force. Any target counters a push or pull automatically while it has the power's cost in Force, is on the ground and is not attacking, charging a weapon or in a hand animation (base_enhanced `CanCounterThrow`); airborne it cannot. Absorb converts the power instead (`WP_AbsorbConversion`), in the air too. The protect thread's "protect cannot be pulled at 20+ Force" is this general counter, not protect. Ragers cannot counter (`g_ragersCanCounterPushPull`, 2020). |
| Protect 3 | 10 Force to start, about 3 a second after; cuts health damage by 80% (from May 2024 75%, after trials at 70%) and costs Force equal to 25% of the damage; with Force short, health loss is 0.45 x damage minus Force, so even 1 Force blocks about half. Turns off after about 20 s. |
| Absorb | 10 Force to start, turns off after about 20 s; absorbing an enemy power gives Force (a pull or drain into absorb feeds the enemy). |
| Team energize | Gives 50 Force (split when several teammates are in range), costs the giver about 25; reach by PVS, not line of sight (a line-of-sight rule was tried in 2024 and reverted). |
| Speed | 50 Force; considered mandatory. |
| Grip | 30 Force, needs 60 to start (32 with `g_gripBuff`); mainly carries enemies into pits; gripping an absorber gives them 30. |
| Drain (reworked) | Costs health not Force; takes up to 33, blocks the target's regen and TE for a few seconds. |
| Spawn | 125 health, 25 armor, full Force; 3 s invulnerability that does not stop pushes into pits; respawn instant; no same spawn twice, and a quick sk excludes the nearest half. |
| Flags | Pits are `trigger_hurt` with damage -1; a dropped flag returns after 30 s; a carrier killed in a pit now counts as a return (earlier it did not). |
| Armor | Rockets, golan primary, bowcaster, stun baton put half their damage through shields; grip, melee and falls ignore shields; protect does not reduce shield damage. |

## How positions play (decision rules stated)

**Carrier.** With no Force, run to safety and call for TE; the first TE buys
Speed, the second protect. Against a fresh chase, let it come and spend its
Force first. Stand where the base can reach you. Near pits, strafe or
circle-run instead of standing in protect. Hand the flag off (by self-kill)
when spent and a teammate is fresh.

**Base.** Keep the carrier's Force up: sk, respawn near the carrier, TE
once or twice, sk again (base TE output can exceed 5,000 a game). Pull
less (absorbed pulls feed the enemy). Choose one plan under 2v1 pressure,
support-spam or clear, and never mix them. Plays dark (TE is dark side);
drain and grip counter rage and protect. Should not take the enemy flag
except in an emergency; may retake a dropped flag.

**Chase.** Rockets and splash are essential; sabers block direct fire.
Position between the enemy offense and its stand instead of following.
Keep absorb up while a light-side threat is near. Prefer leaving the enemy
at 0 Force over low health. A chase who ends up holding the flag a lot
correlates with losing.

**Non-carrying offense.** Do not idle on the enemy stand and do not simply
double chase. When the own chase dies to the carrier and base, attack at
once: the base is spent and the carrier low. Cut the enemy base off from its
carrier (push a respawning base away). Self-kill at low health and Force.

**Breaking a hold.** Starve the carrier of Force (kill or displace the
base, drain), pit it (push, pull, grip, conc knockback), pinch with chase
and camper together, and shoot during protect's off windows (the carrier
drops protect to take a TE).

## Checked against the pilot demos

60 demos (Kejim, Dantooine v3, 2022-2026), `tools/position_resources.py`
and a check script over the same extracts.

- **Energize is 50, and absorb gains were mixed in.** Single-receiver Force
  rises cluster at 49-51 (3,451; a teammate's Force fell with every one)
  and at 17-18 (4,063; the receiver had absorb on in every visible one,
  a teammate paid in 22%). The first is TE, the second Force gained by
  absorbing enemy powers. The pilot's energize figures are corrected
  accordingly ([pilot record](2026-09-27-position-pilot.md#correction-energize-and-absorb-gains-same-day)).
- **Force is in the overlay.** A 2021 message says Force was kept off the
  team overlay; every pilot demo carries it in the armor slot
  (`g_teamOverlayForce`), so the setting came later.
- **Carrier protect uptime 12%** of carried time (21% in its own base) and
  absorb 38%: matches the thread's 10-15% for a top offense.
- **Absorb at pit edges, protect when hurt** (owner, confirmed). A
  carrier within 64 units of a pit edge has absorb on 48% of the time and
  protect 7% on Kejim (192+ units: 27% and 17%); Dantooine v3 39% and 5%
  against 33% and 16%. Below 50 health protect rises to 17% and absorb falls
  to 26-30%; at 100+ health protect is 3-4%. Of 1,076 visible pitted
  carriers, 24% had absorb on and 4% protect: carriers near edges are
  already in absorb, so this is exposure, not protect's safety.
- **Base energize after respawn**: in 41% of base lives one Force spend
  that is not push or pull falls in the first 8 s, two in 10%; 36% of such
  spends are followed by the base's own sk within 3 s. The "TE twice then
  sk" loop is visible but not the majority pattern.
- Already in the pilot record and consistent with the discussions: pits
  are 37% of enemy-caused deaths, splash kills 50% of carriers and sabers
  2%, the carrier's `te`/`help` calls are answered, and a spent carrier
  self-kills to hand the flag to the other offense.

## Candidate named situations for milestone A

From the rules above, each with the evidence the pilot can test it against:

1. Carrier at 0-20 Force, enemy near: run to the base's reach and call
   `te`, do not fight (the 61% answered-call rate).
2. Carrier near a pit edge with an enemy in push range: absorb, since
   absorb stops the push or pull and protect does not; protect when there
   is no pit to be thrown into, or when health is too low to survive the
   damage even at the edge, knowing the risk (owner; the carriers above
   do both).
3. Carrier spent deep in the enemy half, other offense fresh and near:
   hand off by self-kill.
4. Base with the carrier home: TE, then sk and respawn near it, or clear
   the camper; one plan, not both.
5. Chase meeting a protecting carrier: splash, knockback toward pits,
   Force denial; saber fire is wasted.
6. The camper's trade (owner): the camper and the chase should attack the
   enemy carrier together, but the camper must weigh that against staying
   on the enemy stand ready to take the flag if its own carrier dies.
   Taking the flag means the enemy can never cap; returning the own flag
   is what lets the team score. When to camp and when to attack is a
   judgement, not a rule; the community's version is to attack the moment
   the chase dies to the carrier and base, while the base is spent.
   Testable: the camper's position (enemy stand or with the chase) when
   its own carrier dies or the own flag is returned, against what follows
   (a retake, a cap, an enemy cap).

## Not yet checked

Protect's 20-second timeout and the 0.45 x damage rule (need health and
Force traces around hits), grip and drain effects (need their activations
on other players), rage swaps, spawn exclusions, the camper's trade
(situation 6), and map hold-length rankings (need more maps).
