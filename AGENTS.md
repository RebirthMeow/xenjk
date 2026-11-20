# Working in xenjk

OpenJK fork; Bot2 lives under `codemp/game/`. Keep changes portable to other
OpenJK derivatives and preserve unrelated working changes.

## Read by task

Use only the context the task needs; a small edit does not require a project tour.

- Everything in flight and the order to integrate it: `docs/INTEGRATION.md` (a local record, not published).
- Resume movement: [HANDOFF.md](docs/movement/HANDOFF.md) (current state, open and closed items), then [PLAN.md](docs/movement/PLAN.md) for gates.
- Design or physics question: [ARCHITECTURE.md](docs/movement/ARCHITECTURE.md), [PHYSICS.md](docs/movement/PHYSICS.md).
- Carried-forward facts and owner decisions: [LESSONS.md](docs/movement/LESSONS.md).
- Pre-rewrite history (frozen 2026-09-14): [archive](docs/archive/strafe-movement-2026-09-14/README.md).
- Locate a controller or kernel: [Bot2 map](codemp/game/ai_bot2_README.md).
- Convert or upgrade a character model: [the guide](docs/guide/characters-converting.md); the detailed
  lessons are in `docs/character-conversions.md` (a local record, not published).
- Build or collect native evidence: [verification reference](docs/archive/strafe-movement-2026-09-14/STRAFE_MOVEMENT_VERIFICATION.md) (archived; still the build procedure).

The user's direction and docs/movement supersede every dated work order in the archive. Read
history for a specific question; do not rerun completed experiments to resume.

## Completion and boundaries

Continue through implementation and affected verification until the authorized
milestone is complete. Local fixes, retained-evidence analysis and isolated
localhost tests may proceed without approval at each step. Ask when missing
input materially blocks a decision. Status questions do not cancel ongoing work.
Distinguish implementation, measured outcomes, qualification and installation.
Update the handoff when the next action changes and the scorecard when results
change; keep detailed evidence in dated records.

Changes to the installed game require that task scope. Preserve native motion,
resources and lifecycle except for declared fixture setup/interventions. Follow
the inherited archive-only filesystem policy: cleanup is not permission for
permanent deletion. Preserve failed evidence and unrelated working changes.
Agents never delete research data. Cleanup moves regenerable bulk (world graph
snapshots, bot-only world demos, binaries, build trees; a session a person
played is never regenerable, its demos included) to `.codex-trash/` with
`tools/research_shed.py`; only the owner empties it. Logs and results stay put.
List live dependencies in `.codex-research/KEEP.txt`.

## Code contracts

- `ai_bot2*.{c,h}` owns AI policy/integration; `bot2_*.{c,h}` owns shared kernels
  and fixtures. Prefer Bot2-owned logic with small upstream calls. Add new
  build inputs to `codemp/game/CMakeLists.txt`.
- `bot2_state_t` is in `ai_bot2.h`; storage is in `ai_bot2.c`. Before changing
  a shared field's meaning, inspect every consumer; some controllers also own
  private per-client state. Compilation cannot detect semantic drift.
- Inspect the relevant working diff before editing; preserve unrelated work.

## Verification and parallel work

Use affected checks for iteration. Native qualification requires fresh unique
builds and frozen source/binary/settings/case bindings as detailed in the
verification reference. Do not edit measured sources, including comments,
during build or collection. Never weaken gates or omit failures to pass.
Documentation changes need link/consistency checks, not a native build.

Check free space on both drives before launching native runs: worlds stage on C:
(`%LOCALAPPDATA%/xenjk-play`) and are kept on D:. `defense_laps.py` and `play_vs_runner.py`
refuse to start under 20 GB free (`world_retention.require_free`), and `defense_laps.py`
removes a world's staging folder once its copy is written; a stopped world keeps it. Report
a low drive to the owner instead of launching (2026-10-05: unremoved staging filled C:).

Use subagents for bounded independent work with clear ownership. Coordinate
source freezes and process budgets: at most four isolated native outcome worlds (owner, 2026-10-02: eight was too heavy)
with distinct ports/homepaths; timing runs alone. Use hidden launch helpers.

## Search

Use targeted `rg` searches. `.ignore` excludes research outputs, archives and
other checkouts from broad searches, not from Git. Read evidence by exact path
or use `rg --no-ignore --hidden` on that path. Keep portable fixtures/protocols
in `tools/fixtures/` and `docs/`; retain local captures and manifests.
