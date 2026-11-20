# Bike handling files (`.bike`)

A `VH_BIKE` vehicle (`codemp/game/BikeNPC.c`) drives on `codemp/game/bg_bike.c`. Its handling numbers come from its
pack, not from the code:

- **File.** `ext_data/vehicles/<name>.bike`, where `<name>` is the vehicle's `name` in its `.veh` file (the CT-3X's
  `ext_data/vehicles/ct3x.veh` names `ct3x`, so its handling is `ext_data/vehicles/ct3x.bike`).
- **Who reads it.** `bg_vehicleLoad.c` `BG_BikeLoadParams` reads it when the vehicle loads (`G_SetBikeVehicleFunctions`).
  Game and cgame read the same file the same way, so client prediction and the server integrate the same numbers.
  Server and client must carry the same pack, as with any `.veh`.
- **Without it.** The bike drives on `BG_BikeDefaultParams`: a generic sport bike in round numbers (450 kg, 1.45 m
  wheelbase, 170 N m, reverse and six gears to 40 m/s). The console prints `Bike '<name>': no ... the generic handling`.
- **Bad lines.** A line that is not understood is skipped and the rest still applies. The console prints the number of
  skipped lines and the first one's key.

## Format

Plain text, one setting per line. `//` starts a comment; blank lines are ignored. Keys are not case-sensitive. Units
are CP2077's (m, kg, N m, s, deg); `bg_bike.c` converts to JKA units (37.5 u per metre). A file sets only what it
names, over the generic bike.

| Line | Meaning |
|---|---|
| `<field> <value>` | any scalar field of `bikeParams_t` (`codemp/game/bg_bike.h`), by its name: `mass 500`, `maxTilt 38` |
| `<field> <front> <rear>` | the suspension pairs: `suspTravel`, `suspOmega`, `dampBump`, `dampBumpLow`, `dampRebound`, `dampReboundLow`, `dampLowRate`, `dampHighRate` |
| `gear <minSpeed> <maxSpeed> <minRPM> <maxRPM> <torqueMultiplier>` | the gearbox, reverse first, then the forward gears in order. The first `gear` line replaces the whole default gearbox. At least 2 and at most 10 gears. |
| `surface <asphalt\|ground\|sand> <frictionLat> <frictionLong>` | tyre friction multipliers on one surface |
| `curve <name> <x> <y> [<x> <y> ...]` | one curve, keys in ascending x (up to 16), linear between and clamped at the ends. Names: `torque`, `speedToTilt`, `inputToTilt`, `speedToTiltSpeed`, `latTraction`, `latTractionRear`. |

- **speedNorm.** The tilt curves' speed axis follows the top gear's `maxSpeed` unless the file sets `speedNorm`.
- **Integer fields.** `maxCommandMs`, `wheelieAssist` and `airPitchStabilize` take integers.
- **Model fields.** `wheelbase`, `originAhead` (the model origin ahead of the rear axle, m) and `frameClearance`
  belong to the bike's model, not its handling data. A pack carries them for the model it ships.

```
// a short example
mass 480
wheelbase 1.5
suspTravel 0.22 0.30
surface sand 1.0 0.7
gear 0 3 1000 8000 1.0
gear 0 12 1000 8500 1.2
gear 10 20 4000 8500 0.8
curve torque 0 0.8 0.7 1.0 1.0 0.85
```

## The CT-3X file

`tools/bike/ct3x_params.py` writes the CT-3X's file from your own copy of CP2077 (its TweakDB records, read by
`tools/bike/tweakdb.py`) plus the converted model's rig and xenjk's wheelie tuning:

```
python -I tools/bike/ct3x_params.py [ct3x_handling.json] -o ct3x.bike
python -I tools/bike/ct3x_audio.py pack IN.pk3 OUT.pk3 ct3x.bike   # puts it in the bike pack
```

`--from-header OLD/bg_bike_ct3x.h` writes the same file from the header the game was built with before 2026-11-19.
`tools/bike/bike_params_test.c` checks that such a file parses to the same `bikeParams_t` byte for byte as that header
compiled in. `tools/bike/tape_harness.py --bike FILE` runs the Python twin, the C integrator and the pmove test on a
file (without `--bike`, on the generic bike).
