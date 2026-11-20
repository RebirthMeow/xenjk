# Movement physics as the engine implements it

Sources are `codemp/game/bg_pmove.c` and the constants Bot2 already mirrors in
`bot2_movecontrol.c`. Values are from code, not from documentation. Verify any
value marked "check" before relying on it in a planner.

## One law

Every command frame the engine takes a view yaw, a forward/right/up key
state, and the current velocity, and applies:

- **Wish direction** from the view yaw and the key pair. Forward alone is the
  view yaw; forward plus a side key is 45 degrees off it; a side key alone is
  90 degrees off it.
- **Wishspeed** is `ps.speed` scaled by the command magnitude. Backward ground
  input is slowed to three quarters. `ps.speed` is `g_speed` (250 here) times
  1.7 while Force Speed is active, at every Speed level
  (`BG_AdjustClientSpeed`).
- **Ground**: friction removes `max(speed, 100) * 6 * dt` per frame, which
  at the 25 ms step is 15 percent of the speed above stop speed, every
  grounded frame. Then acceleration adds up to `10 * wishspeed * dt` along
  the wish direction, capped so the component along wish does not exceed
  wishspeed. `PM_WalkMove` checks for a jump before applying friction, so a
  frame whose jump is accepted pays no ground friction at all. Excess speed
  therefore survives a landing only if the next jump leaves within a frame
  or two; a grounded run decays to wishspeed in a handful of frames.
- **Air**: no friction; acceleration adds up to `1 * wishspeed * dt` along the
  wish direction, with the same cap on the wish-direction component.

That cap is where strafing comes from. Once velocity along the wish direction
reaches wishspeed, forward input adds nothing. A wish direction rotated off the
velocity keeps the component below the cap and lets the frame add speed. The
gain per air frame is largest when the wish direction is about
`acos(wishspeed / speed)` off the velocity, and the accumulated speed is
unbounded except by geometry and frames in the air. Ground friction pulls
speed back toward wishspeed at 15 percent per grounded frame, so the pattern
is: gain in the air, jump again on the landing frame or the one after, and
accept that every extra grounded frame costs a large slice of the excess.
The archive's "six units per frame" figure is not what the code does; any
measured loss must carry the conditions it was measured under.

The one-frame model of this law is implemented once, in `MoveControl`, and
the executor finds the key pair and view yaw whose predicted result is nearest
a requested velocity. Strafing is therefore an outcome of the request, never
a separately coded angle law.

## Jumps

- A tap gives `JUMP_VELOCITY` (225) of vertical speed and costs nothing.
- A held jump is not a ballistic launch at a higher speed. While jump is held
  and the bot is still below `forceJumpHeight[N]` (96, 192, 384 above the
  takeoff for levels 1 to 3), `PM_CheckJump` rewrites the vertical velocity
  every frame to `225 + strength[N] * (remaining height fraction) / 10`,
  where strength is 420, 590, 840. So a level-3 hold begins at about 309
  vertical and decays toward 225 as it climbs; the ascent is sustained, not
  thrown. Flight time and reach must come from integrating that rule, never
  from a ballistic formula with 840.
- The hold starts costing once the ascent passes the ordinary 32-unit jump
  height: 5 Force immediately and 5 more every 300 ms until landing (levels
  2 and 3). Releasing jump caps vertical speed at 225 while headroom remains.
- The measured 309 u/s tap takeoff in the archive is this rule's first frame,
  not a separate engine constant.
- A saber jump with backward or side input on the ground triggers a flip or
  wall acrobatics. Launches must be forward plus jump; strafing resumes once
  airborne. The executor already enforces this.
- Gravity is 800. Apex height for vertical speed v is `v*v / 1600`; flight
  time to the same height is `2v / 800`.

## Force as a resource

- Pool of 100. Regenerates one point every `g_forceRegenTime` ms (200 here),
  but only while no Force power is active. Speed active blocks regeneration
  for its whole window even though Speed itself drains nothing further.
  Levitation active blocks it too, until landing.
- Force Speed costs 50 to activate and lasts a fixed window per level
  (level 3: 20 s). The 1.7 multiplier applies at every Speed level.
- A held Force Jump drains as described above. A tap does not.
- So the return trip is a budget problem with a dead window: 100 Force buys
  one Speed window, during which nothing regenerates and each held jump
  costs 5 plus 5 per 300 ms of ascent. Regen at five per second resumes only
  after Speed expires and the bot is grounded with no power active.

## What each level implies

Levels are not modes in the code. They are consequences of two inputs, run
speed and jump spend, and the planner treats both as edge costs.

| Input | Level 1 | Level 2 | Level 3 |
|---|---|---|---|
| wishspeed | 250 | 425 | 425 |
| air accel budget per 25 ms frame | 6.25 | 10.6 | 10.6 |
| ground cap under friction | 250 | 425 | 425 |
| jump vertical | 225 tap | 225 tap | sustained hold, 309 down to 225 |
| Force per trip | 0 | 50 once, no regen for 20 s | 50 plus 5 per hold plus 5 per 300 ms |

The air figure is the acceleration vector budget per frame, not a guaranteed
speed gain; how much of it becomes speed depends on the angle the executor
finds. Wishspeed does not change how long a tap stays airborne; it changes
how far the tap travels and where it lands.

Consequences the planner must model rather than assume:

- The same geometry validated at level 1 is not validated at level 2. Higher
  wishspeed means a higher takeoff speed, more horizontal travel in the same
  airtime, a different strafe angle at a given speed, and an overshoot where
  level 1 landed short. Every maneuver edge carries the entry envelope and
  wishspeed it was rolled out with.
- "One number changed" is true of the control law and false of the routes.
  Trajectories and feasible sequences do not scale with wishspeed; they are
  re-derived per wishspeed by rollout.
- Levels are also spending permissions, not only resource availability. A
  level-1 benchmark case starting at 100 Force must still forbid buying Speed
  and holding jumps. The plan carries an explicit spend policy: which edge
  templates are admissible, independent of the pool.
- Level 2 uses the same control law with a larger wishspeed. It is not a
  time-scaled route: changed landing locations, clearance and turn feasibility
  require new rollouts. A lane working at level 1 is not automatically
  executable at level 2.
Required planning semantics (targets, not a claim of whole-trip optimization
in the current implementation):

- Level 3 adds a choice per launch: tap, or hold for some height. Height buys
  reach and clearance; it costs Force and, because the ascent is longer, it
  can cost time on flat ground. The old runner made this choice at the launch
  frame with a ladder of gates. Connected planning should consider its cost
  and subsequent resource needs; beyond the simulated horizon those needs
  remain estimates, not proved whole-trip feasibility.
- Speed activation is itself a maneuver: a 50 Force edge that changes the
  wishspeed while it remains active (20 s at level 3). Selection should weigh
  that purchase within the plan and spend policy. The current purchase logic
  does not establish an optimal allocation over the complete trip.
- Resource depletion should cause replanning from the actual remaining state,
  under the same owner. A tap alternative or a level-1 edge must still be
  feasible from that state; expiry does not guarantee such an alternative
  exists or permit an instantaneous change of airborne trajectory.
