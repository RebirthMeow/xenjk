# Movement build and verification reference

Use for builds, runner changes and native collection, not as mandatory reading
for every edit. Repository boundaries remain in [AGENTS.md](../AGENTS.md).

Run from the repository root in PowerShell. Native research builds require
Python, CMake and Visual Studio 2022 with Win32 C/C++ tools. Game assets are
under `game_directory/Jedi Academy/GameData`.

For native movement qualification after C/C++/header/CMake changes, create a
fresh, uniquely named build without deleting or cleaning an existing one:

```powershell
$movementBuild = '.codex-research/strafe-build-' + [guid]::NewGuid().ToString('N')
python tools/strafe_fixture_build.py --out $movementBuild
```

The helper builds the dedicated engine and game DLL with symbols and records
source/binary hashes. It does not install them into GameData. Use the relevant
runner and audit from the experiment protocol, supplying that exact build.

- For development iteration, rebuild affected targets and use focused behavior
  checks. Fresh builds are required for native qualification, not for every
  documentation edit. Build targets outside the research helper's coverage
  separately when changing client/UI or other disabled components.
- For Python changes, run syntax checks and the relevant existing tests or
  runner/audit checks. For documentation/search configuration, verify links,
  instruction consistency and search behavior; no native build is needed.
- Freeze measured sources during compilation and native collection. Bind
  recordings to the loaded module, build, settings and declared case set.
  Retain failures; do not weaken gates or exclude cases to obtain a pass.
- Complete required checks. Broaden or repeat testing only for new changes,
  failures or unresolved concerns. Timing benchmarks run alone; outcome runs
  may use at most four isolated native worlds with distinct ports/homepaths.
- Use existing hidden launch helpers and private localhost test runtimes.
  Preserve native resources and lifecycle except for declared fixture setup
  or interventions. Changes to the installed game are a separate task scope.
- Keep native server ports outside the host's dynamic UDP range (check with
  `netsh int ipv4 show dynamicport udp` on Windows). On the current host that
  range starts at 49152; use distinct lower ports. Native bind failure may advance
  the server to a neighbor's port, so retain startup/authentication failures as
  setup failures and require the runner-creation acknowledgement before capture.


For the full Python suite when relevant, use the repository root in PowerShell:

```powershell
$env:PYTHONPATH = 'tools'
python -m unittest discover -s tools -p "test_*.py" -t tools
```

Fresh builds are required after measured source changes; an unchanged measured
build may be reused after checking its source bindings. Collect needed controls
before editing their sources; if later recollection is necessary, use a preserved
snapshot/checkout rather than disabling binding checks. Version changed audits
and reanalysis outputs instead of overwriting frozen evidence.

Package-specific regressions and targets live in the
[branch plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md) and
[acceptance contract](STRAFE_MOVEMENT_PROJECT_PLAN.md#acceptance-contract).
