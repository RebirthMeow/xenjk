# Enemy-FC hit-envelope replay

## Objective

Turn a completed player demo into a continuous, hindsight-perfect aiming aid.
While the followed player has a supported projectile weapon and an enemy is
carrying the followed player's flag, draw the set of aim directions that would
produce a direct hit if fired now.  Fast-forward the rest of the demo.

This is a replay-truth oracle.  It answers which launch directions intersect
the movement that was actually recorded; it does not claim that a player could
have predicted long future strafes during the match.

## Display

- Leave the player's normal crosshair unchanged.
- Draw a translucent green hit envelope in screen space.
- Draw a small bright dot at the best validated direction.
- Allow the envelope to form a blob, ribbon, or disconnected islands when
  different future intercept times admit different launch directions.
- Fade long-time-to-contact solutions so short actionable leads are brightest.
- For Golan alt, use opacity as hit probability across the modeled two-ball
  randomness.  A fully opaque sample means all sampled rolls hit.
- Draw nothing when no validated direct-contact solution is available.

## Weapon mapping

- Bryar pistol: primary projectile.
- Imperial repeater: alternate projectile.
- Thermal detonator: alternate throw.
- Rocket launcher: primary rocket.
- Golan/Flechette: alternate two-ball shot.

Use the weapon's real projectile speed, gravity, vertical boost, radius, and
lifetime.  Splash, bounces, and post-bounce contacts remain out of scope.

## Preprocessing and cache

1. Run the entire demo once at high speed.
2. At snapshot endpoints, record enemy flag-carrier identity, origin, decoded
   hitbox, alive state, and flag-carrier intervals.
3. Persist the track in a versioned binary cache beside the existing review
   caches.
4. On the envelope playback pass, load the completed track before the demo
   begins and query future samples for the current carrier.

The cache is tied to the demo timeline and is rebuilt explicitly with the BAT
file's `rescan` argument.

## Solver

For each replay time slice:

1. Build a synthetic shot from the followed player's current origin,
   viewheight, weapon, and the weapon mapping above.
2. Collect the current enemy FC's cached future samples up to the weapon's
   maximum flight time, stopping at death, flag loss, teleport, or a large
   sample gap.
3. Find center intercept candidates over the recorded future path.
4. Around each candidate, sample a narrow pitch/yaw grid that covers the
   projectile-expanded player bounds.
5. Simulate every displayed sample through time.  A sample is accepted only if
   it intersects the swept recorded hitbox and its projectile hull reaches that
   contact without colliding with map geometry.
6. Merge accepted cells for display.  Select the best dot by hit probability,
   then clearance from misses, then earliest contact.

The first implementation uses validated angular cells rather than claiming an
analytic continuous boundary.  Overlapping cells make a filled field while
keeping every field sample backed by an actual trajectory test.

## Playback direction

- When no enemy FC exists, run at high timescale.
- Slow to normal playback shortly before a cached flag pickup so the transition
  is not abrupt.
- Stay at normal speed while a living enemy FC exists.
- Do not freeze or jump between individual shots; the overlay runs continuously
  and is not dependent on fire events.

## Verification

- Build both Release and Debug cgame targets.
- Preprocess the complete Kejim 4v4 demo.
- Confirm the cached FC intervals span pickups through drops/captures/deaths.
- During playback, verify supported-weapon envelopes follow the FC and disappear
  without an FC or without a direct solution.
- For representative cells and every selected best dot, replay the cached
  projectile trajectory and confirm direct swept-hitbox contact with no earlier
  map collision.
- Check Golan probability and deterministic weapons separately.
