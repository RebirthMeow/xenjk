# xenjk

xenjk is a Jedi Academy multiplayer engine with a Vulkan renderer, a strafe-jumping capture-the-flag bot, and
import pipelines for high-detail characters and city-scale levels. It is shared as a resource: take the whole
thing, or lift the one piece you need into your own fork.

## A few things up front

**This is all AI-coded.** Everything xenjk adds was written by AI coding agents, directed and play-tested by one
person. It works, it is measured and reviewed, and it is still worth reading with that in mind.

**Bring your own agent.** xenjk wasn't written with a human reader in mind. The documentation is the agents' working
record (dated, dense, full of measurements and dead ends), there is no installer, and there is no support channel.
It is easiest to work with alongside a coding agent of your own: point it at the repository and let it do the
digging.

**No binaries.** There are no releases, prebuilt executables or packs. You build from source, with your own copy of
Jedi Academy.

**No game content.** Nothing here ships models, textures, levels or audio, and nothing will. The import pipelines
are tools: they run on files you supply, on your own machine. They take real time and space: the largest level
recipe is **4+ hours of processing** and tens of gigabytes of disk, and the others are smaller but still more than
one click.

## What we have, and how you can use it

With those out of the way: here is what is in the box.

- **Renderer.** JKSunny's `rd-vulkan`, extended: sun shadow cascades, reflection probes, SSAO, bloom, PBR material
  sidecars, DDS/BC7, MSAA with alpha to coverage, a 64-bit client, and a render scene that draws dense modern levels
  beside a gameplay BSP. Every map gets the lighting upgrades, stock or imported. [Guide](docs/guide/renderer.md).
- **Characters.** A pipeline from rigged models in other formats to JKA player models (skeleton fit, GLM/GLA,
  materials, packs) and the run-time side: Ghoul2 blending, hair chains, cloth, body springs and character shadows.
  [Pipeline](docs/guide/characters-converting.md), [rendering](docs/guide/characters-rendering.md).
- **Levels.** One pipeline for every level (glTF to `.map` to a patched q3map2 to a pk3), with streamed collision
  chunks for a whole city. [Pipeline](docs/guide/levelport.md), and two worked recipes: a
  [city](docs/guide/night-city.md) and a [town](docs/guide/beauclair.md).
- **Bot2.** A movement runner that builds a movement graph from a map's own collision, plans strafe-jump routes over
  it and checks every hop with the game's `Pmove` before leaving the ground, plus defenders that guard, chase and
  camp. [Source map](codemp/game/ai_bot2_README.md), [movement records](docs/movement/README.md).
- **Client features.** Visual sound indicators ([docs](docs/SOUND_INDICATORS.md)), Ogg Theora cinematics
  ([docs](docs/THEORA.md)), and visibility cvars (`r_picmip_world_only`, `cg_enemyModel`, `cg_enemyColors`).

**Where to start.** [`docs/guide/`](docs/guide/README.md) is the path from a stock engine to xenjk, one chapter per
piece. Each chapter says what the piece does, which files to take and which stock functions they hook, how to run
its pipeline on your own files, and what is still open. [`AGENTS.md`](AGENTS.md) holds the working rules the agents
follow here; hand it to yours along with the chapter you care about.

All testing runs on localhost; nothing here connects to a live server. Bots run on the server and show as bots.

## Building

You need Windows, Visual Studio 2022 with the C++ workload, and CMake 3.21 or newer. There are two ways in.

**With the helper script.** From the repository folder:

```cmd
set JK_GAMEDATA=C:\path\to\Jedi Academy\GameData
build_and_install.bat
```

That builds the 64-bit client and lists what it would copy into your `GameData`, and where. Nothing is copied yet.
When the list looks right:

```cmd
build_and_install.bat install
```

Any file it replaces is moved to a `xenjk_backup` folder beside `GameData` first, so you can always put the old one
back. Add `win32` for the 32-bit client, `debug` for a Debug build, `theora` for Ogg Theora cinematics, or `clean`
to start the build folder over. The script ends by printing the command that starts the game.

**With CMake or Visual Studio directly.** `CMakePresets.json` has `x64` and `win32`:

```cmd
cmake --preset x64
cmake --build --preset x64-release
```

or open the folder in Visual Studio and pick a preset from the toolbar. The output is in `build\x64\Release`. Copy
it the way the script lists: the exe, `rd-vulkan` and `SDL2.dll` in `GameData\bin64` (32-bit: `GameData` itself),
and `cgame`, `jampgame` and `ui` in `GameData\base`.

Windows is the only platform built here.

## Repository map

| Path | What it is |
|---|---|
| [`docs/guide/`](docs/guide/README.md) | The guide, one chapter per piece |
| [`AGENTS.md`](AGENTS.md) | Working rules for coding agents in this repository |
| `codemp/rd-vulkan/` | The Vulkan renderer |
| `codemp/game/bot2_*`, `ai_bot2*` | Bot2: the runner's shared parts, and the AI and its hooks into the game |
| `tools/characters/`, `tools/hair/`, `tools/cloth/` | Character pipeline, hair chains, cloth |
| `tools/levelport/` | The level pipelines |
| `docs/` | Dated records: measurements, designs, reviews, dead ends |

## Credits

Built on [OpenJK](https://github.com/JACoders/OpenJK) and the original Raven Software / Activision source release.
The Vulkan renderer comes from JKSunny's work. Smaller pieces are ported from EternalJK, TaystJK, base_enhanced and
NewJK; each is attributed where it lands. Third-party libraries live in `lib/` under their own licences.

## License

GPLv2, as the source it is built on. See [LICENSE.txt](LICENSE.txt). Jedi Academy game assets are not included and
are not covered by this license. Games named in the documentation belong to their owners; xenjk is not affiliated
with or endorsed by any of them.
