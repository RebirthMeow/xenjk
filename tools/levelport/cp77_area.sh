#!/bin/bash
# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
# cp77_area.sh <area> <tag> [--map NAME] -- every stage of a CP2077 level port, in order, then the level checks.
# One recipe so no stage is forgotten (the Watson slice once skipped `fetch --embedded`, the roads the sectors
# embed, and its streets came out as holes). Run from the repository root; logs to <work>/area-<tag>.log; stops at
# the first failing stage. Outputs: <research>/levelport/build/<map>-<tag>/ (gameplay map) and
# <map>_scene-<tag>/ (render scene), checks in <research>/levelport/look/levelcheck-<area>-<tag>/; <research> is
# $XENJK_RESEARCH, else .codex-research (tools/levelport/paths.py).
#   sectors      streaming sectors in the area box (+ blocks: default quest variants)
#   scan         placements.json
#   fetch        meshes with materials; --ents entity templates; --lods every LOD; --embedded the meshes the
#                sectors embed (bent roads, terrain); --embedded-ml their multilayer setups
#   scene        the render scene
#   build        the collision glb; gltf2map + the patched q3map2: the gameplay map
#   checks       levelcheck spawns, aerial holes, the collision compare and its clusters, a player dropped on every
#                flagged surface (5,000 of the drops traced by the engine too: the game module's levelcheck_trace,
#                a jampgame-only x64 build named by $XENJK_X64_GAME, see levelcheck.py X64_GAME)
# Extra cp2gltf arguments for every stage: CP2GLTF_ARGS (e.g. "--half 400"). Skip the sector fetch with
# SKIP_SECTORS=1 when the work folder already has this area's sectors.
set -u
AREA=$1
TAG=$2
MAP=cp_$AREA
if [ $# -ge 3 ]; then
    [ "$3" = --map ] && [ $# -eq 4 ] || { echo "usage: cp77_area.sh <area> <tag> [--map NAME]" >&2; exit 2; }
    MAP=$4
fi
cd "$(dirname "$0")/../.." || exit 1
R=$(pwd)
RS=${XENJK_RESEARCH:-$R/.codex-research}
W=$RS/levelport/cp77/$AREA
B=$RS/levelport/build
LC=$RS/levelport/look/levelcheck-$AREA-$TAG
mkdir -p "$W"
L=$W/area-$TAG.log
: > "$L"
X=${CP2GLTF_ARGS:-}
step() { echo "== $* $(date +%H:%M:%S)" >> "$L"; "$@" >> "$L" 2>&1; r=$?; echo "exit=$r $(date +%H:%M:%S)" >> "$L"; [ $r -eq 0 ] || { echo "FAILED" >> "$L"; exit $r; }; }
C="python -u tools/levelport/cp2gltf.py"
A="--area $AREA $X"
[ "${SKIP_SECTORS:-0}" = 1 ] || step $C sectors $A
step $C scan $A
# --ents first: it adds the entities' mesh components (traffic lights, doors, signs, crossings) to what the plain
# fetch exports with materials (the Watson slice ran it after: 7,102 placements came out with no materials)
step $C fetch --ents $A
step $C fetch $A
step $C fetch --lods $A
step $C fetch --embedded $A
step $C fetch --embedded-ml $A
# the render scene and the collision build need nothing from each other: side by side (each its own log), about half
# an hour saved on the Watson slice
step_bg() { local log=$1; shift; echo "== $* $(date +%H:%M:%S) (log $log)" >> "$L"; "$@" > "$log" 2>&1; echo $? > "$log.exit"; }
rm -f "$W/area-$TAG.scene.log.exit" "$W/area-$TAG.build.log.exit"     # (an earlier run's are not this run's)
step_bg "$W/area-$TAG.scene.log" $C scene $A --tag "$TAG" --map "$MAP" &
step_bg "$W/area-$TAG.build.log" $C build $A --no-render --min-size 1 --tag "$TAG" --col-tris 0 --col-prop-tris 0 &
wait
for s in scene build; do
    r=$(cat "$W/area-$TAG.$s.log.exit" 2>/dev/null || echo missing)
    echo "$s exit=$r $(date +%H:%M:%S)" >> "$L"
    [ "$r" = 0 ] || { echo "FAILED ($s, see area-$TAG.$s.log)" >> "$L"; exit 1; }
done
RADIUS=$(python -c "import sys; sys.path.insert(0, 'tools/levelport'); import cp2gltf; print(cp2gltf.AREAS['$AREA']['radius'] * 37.5)")
step python -u tools/levelport/gltf2map.py "$W/$AREA-$TAG.glb" "$MAP" --out "$B/$MAP-$TAG" --sky cp_kabuki_haze \
    --sky-haze "66 43 40 61 37 36" --sun "0.55 0.65 1 80 200 40" --skylight 150 --worldspawn _ambient=20 \
    --light fast --vis none
step python -I tools/levelport/levelcheck.py spawns "$B/$MAP-$TAG/zz_$MAP.pk3" --out "$LC"
step python -I tools/levelport/levelcheck.py aerial "$B/$MAP-$TAG/zz_$MAP.pk3" "$B/${MAP}_scene-$TAG/zz_${MAP}_scene.pk3" \
    "$LC" --map "$MAP" --radius "$RADIUS"
# (--zmax 600: this recipe's build keeps cp2gltf's default --col-height of 16 m)
step python -I tools/levelport/levelcheck.py compare "$B/$MAP-$TAG/zz_$MAP.pk3" \
    "$B/${MAP}_scene-$TAG/zz_${MAP}_scene.pk3" "$LC/collide" --zmax 600
step python -I tools/levelport/levelcheck.py collide "$LC/collide/cats.npz" "$LC"
step python -I tools/levelport/levelcheck.py drop "$B/$MAP-$TAG/zz_$MAP.pk3" "$LC/collide" "$LC" --engine 5000 \
    --scene "$B/${MAP}_scene-$TAG/zz_${MAP}_scene.pk3" --map "$MAP"
echo "ALL_DONE $(date +%H:%M:%S)" >> "$L"
