"""make_cfg_npc.py <kind> <name>: writes home/base/<name>.cfg for eve2b_np with the cloth sidecar.
The phases of the stock-skeleton cloth cfg writer, with the eve2b_np spot (mp/ffa1 3032 832, open floor) and togglemenu (the window is hidden, a posted ESC needs focus).
kinds: verify (normal play, one teleport at the start and one at the end: reset causes), shots (on/off pairs at slowed
time), fps (cloth on/off frame-rate phases, no teleports between phases)."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys
W = str(REPO / '.codex-research' / 'eve2b-np-cloth-20261012')
kind, name = sys.argv[1], sys.argv[2]
SPOT = 'setviewpos 3032 832 860 90'

HEAD = f'''set developer 0
set model "eve2b_np/default"
set bot_enable 0
set g_gametype 0
set g_maxForceRank 7
set forcepowers "7-1-000000000000000330"
set com_maxfps 125
set r_clothStats 2
set r_cloth 1
devmap mp/ffa1
wait 2500
team free
wait 750
echo EVE_MENU_UP
wait 300
togglemenu
wait 1500
set model eve2b_np/default
wait 600
notarget
set cg_thirdPerson 1
set cg_thirdPersonVertOffset -22
set cg_thirdPersonRange 150
set cg_thirdPersonAngle 0
{SPOT}
wait 250
'''

def pair(tag):
    """the same moment with the sim and with plain skinning: time slowed 50x around the capture"""
    return (f'set timescale 0.02\nwait 3\nscreenshot {name}_{tag}_on\nwait 3\nset r_cloth 0\nwait 4\n'
            f'screenshot {name}_{tag}_off\nwait 3\nset r_cloth 1\nset timescale 1\n')

B = ''
if kind == 'verify':
    # every reset must be explained: first spawn of the instance, the setviewpos above (if the cloth was already live),
    # and the deliberate setviewpos at the end. Normal motion in between, with screenshots (long frames) mixed in.
    B += 'set r_clothDebug 1\nset r_clothStats 2\nset cg_thirdPersonAngle 160\necho PHASE_BEGIN normal_play\nwait 400\n'
    B += 'set cl_yawspeed 140\n+forward\nwait 300\n+left\nwait 600\n-left\n'
    for i in range(6): B += f'wait 20\nscreenshot {name}_run{i}\n'
    B += '+moveup\nwait 300\n-moveup\n-forward\nwait 120\n'
    for i in range(5): B += f'+moveup\nwait 40\n-moveup\nwait 120\n'
    B += '+forward\nwait 60\n+moveup\nwait 80\n-moveup\nwait 200\n-forward\nwait 100\n'
    B += '+attack\nwait 400\n-attack\n+forward\n+attack\nwait 300\n-attack\n+left\nwait 300\n-left\n-forward\nwait 200\n'
    B += 'set com_maxfps 30\n+forward\n+left\nwait 150\n-left\n-forward\nset com_maxfps 125\nwait 100\n'
    B += 'echo PHASE_END normal_play\nwait 300\necho PHASE_BEGIN teleport\nsetviewpos 3032 1432 860 90\nwait 300\necho PHASE_END teleport\nwait 300\n'
elif kind == 'shots':
    B += 'set cg_thirdPersonAngle 180\nwait 120\n' + pair('idle_front')
    B += 'set cg_thirdPersonAngle 90\nwait 60\n' + pair('idle_side')
    B += 'set cg_thirdPersonAngle 0\nwait 60\n' + pair('idle_back')
    B += 'set cg_thirdPersonRange 90\nset cg_thirdPersonVertOffset -25\nset cg_thirdPersonAngle 200\nwait 60\n' + pair('idle_close')
    B += 'set cg_thirdPersonRange 150\nset cg_thirdPersonVertOffset -22\n'
    B += 'set cg_thirdPersonAngle 30\n+forward\nwait 60\n' + pair('run_a') + 'wait 12\n' + pair('run_b')
    B += 'set cg_thirdPersonAngle 100\nwait 6\n' + pair('run_side')
    B += '-forward\nwait 10\n' + pair('stop') + 'wait 60\n'
    B += f'{SPOT}\nwait 30\nset cl_yawspeed 720\nset cg_thirdPersonAngle 150\n+forward\nwait 40\n+left\nwait 14\n' + pair('turn_a') + 'wait 10\n' + pair('turn_b') + '-left\nwait 8\n' + pair('turn_c') + '-forward\nwait 60\n'
    B += f'{SPOT}\nwait 30\nset cg_thirdPersonAngle 120\n+moveup\nwait 18\n' + pair('jump_a') + 'wait 12\n' + pair('jump_b') + '-moveup\nwait 25\n' + pair('jump_land') + 'wait 60\n'
    B += f'{SPOT}\nwait 30\nset cg_thirdPersonAngle 90\n+forward\nwait 40\n+moveup\nwait 16\n' + pair('flip_a') + 'wait 10\n' + pair('flip_b') + 'wait 10\n' + pair('flip_c') + '-moveup\n-forward\nwait 60\n'
    B += f'{SPOT}\nwait 30\nset cg_thirdPersonAngle 160\n+attack\nwait 12\n' + pair('atk_a') + 'wait 8\n' + pair('atk_b') + 'wait 8\n' + pair('atk_c') + '-attack\nwait 40\n'
    B += 'set cg_thirdPersonAngle 60\n+forward\n+attack\nwait 30\n' + pair('runatk_a') + 'wait 10\n' + pair('runatk_b') + '-attack\n-forward\nwait 40\n'
    B += '+movedown\nwait 120\n' + pair('crouch') + '-movedown\nwait 60\n'
elif kind == 'fps':
    # frame rate with the sim on and off; no screenshots and no teleports inside the measured phases
    B += 'set com_maxfps 0\nset r_clothStats 2\nset cg_thirdPersonAngle 160\nwait 200\n'
    for ph in ('idle', 'circle', 'attack'):
        for on in (1, 0, 1):
            B += f'set r_cloth {on}\necho PHASE_BEGIN {ph}_cloth{on}\n'
            if ph == 'circle': B += 'set cl_yawspeed 140\n+forward\n+left\n'
            if ph == 'attack': B += '+attack\n'
            B += 'wait 2500\n'
            if ph == 'circle': B += '-left\n-forward\n'
            if ph == 'attack': B += '-attack\n'
            B += f'echo PHASE_END {ph}_cloth{on}\nwait 100\n'
    B += 'set com_maxfps 125\n'
B += 'echo EVE_DONE\nquit\n'
open(rf'{W}\home\base\{name}.cfg', 'w', encoding='utf-8', newline='\n').write(HEAD + B)
print('wrote', name)
