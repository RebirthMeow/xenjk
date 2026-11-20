"""make_cfg_np.py <model> <prefix> <cfgname> -- eve8.cfg shot list (as the stock-skeleton make_cfg.py) on mp/ffa1 at 3032 832 (open floor; the ffa3 corner clipped the camera) plus crouch,
saber-style close-ups (saberAttackCycle) and a hands close-up. Writes <work>/home/base/<cfgname>.cfg."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import os
import sys
model, prefix, cfgname = sys.argv[1:4]
H = f'''set developer 1
set model "{model}/default"
set bot_enable 0
set g_maxForceRank 7
set forcepowers "7-1-000000000000000330"
set g_gametype 0
devmap mp/ffa1
wait 2500
team free
wait 750
echo EVE_MENU_UP
wait 300
togglemenu
wait 1500
set model {model}/default
wait 600
notarget
set cg_thirdPerson 1
set cg_thirdPersonVertOffset -22
set cg_thirdPersonRange 150
setviewpos 3032 832 860 90
wait 60
'''
P = prefix
B = ''
for name, ang in (('front', 180), ('front34', 135), ('left', 90), ('back', 0), ('back34', 45), ('right', 270), ('front34r', 225)):
    B += f'set cg_thirdPersonAngle {ang}\nwait 40\nscreenshot {P}_idle_{name}\n'
B += f'set cg_thirdPersonRange 70\nset cg_thirdPersonVertOffset 10\nset cg_thirdPersonAngle 160\nwait 40\nscreenshot {P}_closeup_face\n'
B += f'set cg_thirdPersonRange 90\nset cg_thirdPersonVertOffset -25\nset cg_thirdPersonAngle 200\nwait 40\nscreenshot {P}_closeup_skirt\n'
B += f'set cg_thirdPersonRange 150\nset cg_thirdPersonVertOffset -22\nset cg_thirdPersonAngle 90\n+forward\nwait 45\nscreenshot {P}_run_a\nwait 10\nscreenshot {P}_run_b\nwait 10\nscreenshot {P}_run_c\nwait 10\nscreenshot {P}_run_d\n-forward\nwait 30\n'
B += f'setviewpos 3032 832 860 90\nwait 30\n+moveup\nwait 22\nscreenshot {P}_jump_a\nwait 10\nscreenshot {P}_jump_b\n-moveup\nwait 90\n'
B += f'+attack\nwait 14\nscreenshot {P}_attack_a\nwait 8\nscreenshot {P}_attack_b\nwait 8\nscreenshot {P}_attack_c\n-attack\nwait 30\n'
# additions: crouch idle / crouch walk, land, saber stances and hands
B += f'wait 400\nsetviewpos 3032 832 860 90\nwait 400\n+movedown\nwait 120\nscreenshot {P}_crouch_idle\n+forward\nwait 40\nscreenshot {P}_crouch_walk\n-forward\n-movedown\nwait 300\n'
B += f'setviewpos 3032 832 860 90\nwait 300\n+attack\nwait 14\nscreenshot {P}_gattack_a\nwait 10\nscreenshot {P}_gattack_b\n-attack\nwait 300\n'
B += f'set cg_thirdPersonRange 70\nset cg_thirdPersonVertOffset -34\nset cg_thirdPersonAngle 150\nwait 40\nscreenshot {P}_hands_style_a\n'
B += f'set cg_thirdPersonAngle 100\nwait 30\nscreenshot {P}_hands_style_a_side\n'
for st in 'bc':
    B += f'saberAttackCycle\nwait 200\nset cg_thirdPersonAngle 150\nwait 30\nscreenshot {P}_hands_style_{st}\nset cg_thirdPersonAngle 100\nwait 30\nscreenshot {P}_hands_style_{st}_side\n'
B += 'echo EVE_DONE\nquit\n'
open(os.path.join(str(REPO / '.codex-research' / 'eve2b-nativeprop-20261012'), 'home', 'base', f'{cfgname}.cfg'), 'w').write(H + B)
