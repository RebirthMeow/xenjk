# Fixed-frame Golan direct-hit lesson

## Source encounter

- Demo: `xen_kejim_4v4.dm_26`, a completed Kejim 4v4 game from <player>'s POV.
- Frozen server time: `1217444` (about `11:13.134` relative to the demo start).
- Target: the enemy flag carrier, client 1.
- Measured from the reconstructed firing origin: about 492 units away, moving at about 432 ups over the immediate post-freeze interval, with 418 ups lateral motion and 229 ups away from the red flagstand. The FC remains level while running laterally through both forward velocity windows; this is not a jump apex or reversal frame.
- The recorded future FC path is used as hindsight. The lesson therefore answers the counterfactual question “which launch direction would have directly intersected the player actually recorded in this demo?” It does not claim that a live player could predict later strafes.

## Seven captured layers

1. The fixed source frame, target, range, velocity components, immediate-future velocity arrow, and short recorded future path.
2. A 900-ups straight rocket baseline.
3. A 1050-ups straight projectile, isolating the effect of speed.
4. A 1050-ups projectile with gravity and the fixed 10-degree Golan lift applied together; these effects mostly oppose one another.
5. The validated one-ball direct-hit probability heatmap using the real Golan speed, lift, sideways spread, lifetime, and gravity randomness. Every displayed aim has at least one sampled random outcome that directly hits.
6. The two-independent-ball probability heatmap, converting each point to `P(at least one hit) = 1 - (1 - p)^2`.
7. A fullscreen two-ball probability analysis. It enlarges the gradient, labels exact brightness levels, splits the selected best aim into `P(both) = p^2`, `P(exactly one) = 2p(1-p)`, and `P(neither) = (1-p)^2`, and quantifies the connected 40%+ aiming zone in degrees and FOV-100 screen pixels.

All hit fields use projectile-radius-expanded player bounds, recorded target movement, projectile gravity where applicable, and world collision traces. The cyan path ends at the selected recorded contact time for each layer, making the relationship between target motion and the green launch field directly inspectable. Splash damage and post-bounce contacts are deliberately excluded.

## Run

Launch `run_golan_lesson.bat`. It archives any previous screenshot set, uses FOV 100, fast-forwards to the selected encounter, freezes one demo frame, writes the seven ordered JPGs under `build/aimfeedback-kejim-xen-4v4-20200829/base/screenshots`, and exits.

The encounter time remains selectable through `cg_aimFeedbackLessonTime` for later lesson scenes. Set `cg_aimFeedback` to `2` to print each solver stage's validated-cell count and selected angular correction to `qconsole.log`.
