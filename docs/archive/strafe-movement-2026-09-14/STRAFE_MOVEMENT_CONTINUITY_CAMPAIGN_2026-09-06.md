# Defended movement continuity comparison

Declared before collection. This campaign is a cheaper-model execution handoff
for M3a package B. The lead agent owns movement diagnosis, candidate selection,
source changes, and native process-budget coordination. An executor may collect
and audit the cases below exactly as written; it must not tune the controller,
change the matrix, discard a run, or interpret a failed case as authority to edit
movement code.

## Frozen question and builds

### September 8 repetition correction

New campaigns declare `pre-actor-v1` decision seeds, one distinct value per
case, with the same mapping across arms and independent of ports. The smoke
runner supplies `bot_playDecisionSeed` and requires the native `SEED` response
before proceeding. This initializes both game RNG streams once immediately
before the first runner is queued in a fresh dedicated world. It does not
control map-load randomness or make combat trajectories paired trials.
Ordinary unseeded play retains the engine initialization. Live clients, a
started play log, malformed seeds and listen servers refuse explicit seeding.

Freeze after all helper/protocol edits: launch now verifies frozen helper
hashes as well as sources and payloads. Audit cross-checks the native seed
acknowledgement, provenance and case settings. Identical complete runner tapes
within a team/rate cell invalidate those repetitions; retain the recordings.
Historical unseeded campaigns remain auditable with the duplication check.
The older manual examples below document unseeded collections; use the
campaign freezer/runner for new qualification.

R6 candidate and baseline red 1x r1/r3 tapes were identical, but the
corners-only tapes were distinct. Unique ports do not establish independent
random streams. Startup-clock seed collision is plausible; the old logs did
not record seeds, so it is not a demonstrated retrospective cause.

Compare defended movement continuity on `mp/ctf_kejim` between the fixed
baseline build
`.codex-research/strafe-build-stall-fix-lateral-20260906` and one candidate
fresh build named by the lead. Each build gets the same 12 declared case slots:
three repetitions for each runner team (`red`, `blue`) at each rate (`1`, `5`).
Every case observes 600 native game seconds after all actors and the requested
rate are confirmed. One Runner_v2 faces two legacy Bot2 enemies, with the odd
enemy forced to CHASE and the even enemy forced to BASE by the existing native
fixture. Use ordinary map loading, no corner/drop/blocker intervention, no pose,
resource, health, flag, or lifecycle reset, and no listen server.

This is a defended comparison, not a deterministic paired trial. Native spawn,
combat, and objective histories vary. Repetition number is a predeclared case
slot, not a random seed and not permission to pair individual trajectories.

| Case IDs | Team | Rate | Native step | Repetitions | Game time each |
|---|---|---:|---:|---:|---:|
| `red-1x-r1..r3` | red | 1x | 25 ms (`sv_fps=40`) | 3 | 600 s |
| `blue-1x-r1..r3` | blue | 1x | 25 ms (`sv_fps=40`) | 3 | 600 s |
| `red-5x-r1..r3` | red | 5x | 25 ms (`sv_fps=200`) | 3 | 600 s |
| `blue-5x-r1..r3` | blue | 5x | 25 ms (`sv_fps=200`) | 3 | 600 s |

The baseline must be collected before any C, header, or CMake edit because
`strafe_play_smoke.py` verifies the build's recorded source hashes against the
current checkout. Freeze the checkout for the full baseline collection. Every
baseline output copies the hash manifest and records the verified manifest,
binaries, runner, fixtures, settings, and requested duration in
`provenance.json`. A hash manifest is not a source snapshot: before launching,
use `strafe_continuity_campaign.py freeze` to copy every build-listed source
byte into the campaign's `source-snapshot/` tree and verify every SHA-256. The
campaign declaration also freezes the runner, auditor, protocol, personality,
bot file, and route-catalog hashes.
After the baseline is complete, the lead may change sources and create a fresh
candidate with `tools/strafe_fixture_build.py`. Freeze that candidate's measured
sources throughout its 12 cases. Never bypass or weaken provenance verification.

## Portable execution

Run from the repository root in PowerShell. Choose an unused private port block
and output roots; do not reuse an existing output directory. The example ports
are declarations local to this invocation and may be replaced as a whole after
checking availability. At most four native worlds may run concurrently. The
smoke runner stages binaries and the fixed assets from
`game_directory/Jedi Academy/GameData` and `tools/fixtures/`; it does not install
the build into GameData.

Before collection, validate the tools and build without launching a world:

```powershell
python -m py_compile tools/strafe_play_smoke.py tools/strafe_opposition_audit.py
python tools/strafe_opposition_audit.py --self-test
python tools/strafe_play_smoke.py --help
$baselineBuild = Resolve-Path -LiteralPath '.codex-research/strafe-build-stall-fix-lateral-20260906'
$sourceManifests = Get-ChildItem -LiteralPath $baselineBuild.Path -Filter '*.sources.json' -File
if ($sourceManifests.Count -ne 1) { throw "Expected one source manifest, found $($sourceManifests.Count)" }
$sourceManifests | Select-Object FullName, Length
```

Declare a helper for one case. `--seconds 720` is a wall-clock watchdog, not the
measurement duration. The runner stops only after all actors have observed the
declared 600 game seconds; an early watchdog or process exit is a retained failed
case.

```powershell
$campaignRoot = '.codex-research/strafe-continuity-baseline-20260906'
$portBase = 29600
$baselineBuild = (Resolve-Path -LiteralPath '.codex-research/strafe-build-stall-fix-lateral-20260906').Path

function Start-ContinuityCase {
    param([string]$Build, [string]$Root, [string]$Team,
          [string]$Rate, [int]$Rep, [int]$Port)
    $caseId = $Team + '-' + $Rate.Replace('.', 'p') + 'x-r' + $Rep
    $caseOut = Join-Path $Root $caseId
    if (Test-Path -LiteralPath $caseOut) {
        throw "Refusing to reuse retained case output: $caseOut"
    }
    $watchdog = if ($Rate -eq '1') { 720 } else { 240 }
    Start-Process -FilePath 'python' -WindowStyle Hidden -PassThru -ArgumentList @(
        'tools/strafe_play_smoke.py', '--build', $Build, '--out', $caseOut,
        '--team', $Team, '--enemies', '2', '--timescale', $Rate,
        '--game-seconds', '600', '--seconds', "$watchdog", '--port', "$Port")
}
```

Run cases in bounded waves and wait for each wave before reusing capacity. A
safe first wave is the three red 1x repetitions plus red 5x repetition 1; the
next waves cover the remaining declared slots. Record the returned process IDs
and inspect their exit codes. These commands launch the entire baseline matrix
in three waves of four:

```powershell
$wave1 = @(
    Start-ContinuityCase $baselineBuild $campaignRoot red 1 1 ($portBase + 0)
    Start-ContinuityCase $baselineBuild $campaignRoot red 1 2 ($portBase + 1)
    Start-ContinuityCase $baselineBuild $campaignRoot red 1 3 ($portBase + 2)
    Start-ContinuityCase $baselineBuild $campaignRoot red 5 1 ($portBase + 3))
$wave1 | Wait-Process
$wave1 | Select-Object Id, ExitCode

$wave2 = @(
    Start-ContinuityCase $baselineBuild $campaignRoot blue 1 1 ($portBase + 4)
    Start-ContinuityCase $baselineBuild $campaignRoot blue 1 2 ($portBase + 5)
    Start-ContinuityCase $baselineBuild $campaignRoot blue 1 3 ($portBase + 6)
    Start-ContinuityCase $baselineBuild $campaignRoot red 5 2 ($portBase + 7))
$wave2 | Wait-Process
$wave2 | Select-Object Id, ExitCode

$wave3 = @(
    Start-ContinuityCase $baselineBuild $campaignRoot red 5 3 ($portBase + 8)
    Start-ContinuityCase $baselineBuild $campaignRoot blue 5 1 ($portBase + 9)
    Start-ContinuityCase $baselineBuild $campaignRoot blue 5 2 ($portBase + 10)
    Start-ContinuityCase $baselineBuild $campaignRoot blue 5 3 ($portBase + 11))
$wave3 | Wait-Process
$wave3 | Select-Object Id, ExitCode
```

Do not rerun into the same directory. If a case fails, retain it and, if the
lead requests a retry, use a separately named supplemental directory. The
original remains part of the declared result. Use the same helper and matrix
for the candidate, changing only `$campaignRoot`, `$portBase`, and `$Build` to
the lead-provided fresh build. Record those three resolved values with the
handoff notes.

Audit a completed 12-case build together:

```powershell
$caseDirs = Get-ChildItem -LiteralPath $campaignRoot -Directory |
    Where-Object { $_.Name -match '^(red|blue)-(1|5)x-r[123]$' } |
    Sort-Object Name
if ($caseDirs.Count -ne 12) { throw "Expected 12 declared cases, found $($caseDirs.Count)" }
$auditOut = Join-Path $campaignRoot 'opposition-audit.json'
python tools/strafe_opposition_audit.py --out $auditOut @($caseDirs.FullName)
```

For every case, require `summary.json` to report `game_duration_complete=true`,
`rate_confirmed=true`, `roles_verified=true`, `enemies_observed=2`, requested
and observed rate equal to the case declaration, at least 600 observed game
seconds, and all sampled native frames at 25 ms. A tooling, provenance, role,
rate, duration, process, or audit failure fails that case; it is not silently
excluded from group comparison.

## Frozen comparison rules

Use the existing auditor unchanged. Its time-weighted locomotion definitions
remain: grounded 3D speed below 20 is idle, 20 to below 300 is walking, and
goal/flag-state waiting is reported separately. The travel denominator excludes
only classified grounded goal waiting. Distant confinement is an alive episode
lasting at least five game seconds within 64 units of its initial sampled anchor
while every known goal distance is greater than 96 units. Report captures,
deaths, flag state, longest idle/walking spans, and confinement coordinates as
context; they are not substitutes for continuity.

Evaluate the fixed baseline and candidate as complete 12-run groups and also
show the four predeclared team/rate cells of three runs each. Retain and report
every run. For each group and cell, report sums of alive observed seconds,
grounded idle elsewhere seconds, grounded walking seconds, classified goal-wait
seconds, and counts/durations of distant confinement. Compute group travel idle
and walking percentages from summed seconds and the summed travel denominator;
do not average per-run percentages or weight teams/rates equally after data are
missing.

The candidate passes this continuity comparison only when all of the following
hold:

1. All 12 candidate cases and all 12 baseline cases are valid and retained.
2. No candidate case contains an unexplained distant-confinement episode of
   five game seconds or longer. Every detected episode is a failure unless the
   retained telemetry/demos support a concrete native goal-arrival or declared
   flag-state wait explanation; merely naming combat, congestion, or randomness
   is not an explanation. The auditor's `distant_stalled` classification is not
   automatically excused.
3. In every predeclared team/rate cell, the candidate's travel idle percentage
   does not exceed that baseline cell's travel idle percentage, and its travel
   walking percentage does not exceed that baseline cell's travel walking
   percentage. Report the same comparison across all 12 runs as context.
   This is a nonregression comparison against the frozen baseline, not a new
   arbitrary percentage threshold.
4. Show each team/rate cell and the full group. The aggregate cannot hide a cell
   regression. Because three unpaired runs are descriptive rather than a statistical
   equivalence test, do not invent a significance test, tolerance, or percentage
   gate after seeing results.

These rules assess defended continuity and locomotion only. A pass does not
close matched-resource speed references, cached-Speed expiry handoff,
travel-facing/ordinary-jump regressions, the richer continuous-workload cost
gate, M3a as a whole, or combat readiness. Four-Hz samples can miss larger
moving loops; review retained demos and telemetry when the metrics or case
histories disagree.

## Handoff record

The executor returns the resolved build and output paths, port block, process
exit codes, the 24 case summaries, both audit paths, a cell/group comparison,
and links to every detected confinement episode. It explicitly lists invalid,
failed, and supplemental runs. The lead then diagnoses movement causes and
decides whether further code work or native collection is justified. Do not
install a candidate or update the active plan/scorecard from this execution
handoff alone.
