from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import os
import sys
model, prefix, cfgname = sys.argv[1:4]
H = f'''set developer 1
set model "{model}/default"
set bot_enable 0
set g_gametype 0
devmap mp/ffa3
wait 2500
team free
wait 750
echo EVE_MENU_UP
wait 1500
set model {model}/default
wait 600
notarget
set cg_thirdPerson 1
set cg_thirdPersonVertOffset -22
set cg_thirdPersonRange 150
setviewpos 1664 1612 -19 90
wait 60
'''
B = ''
for name, ang in (('front', 180), ('front34', 135), ('left', 90), ('back', 0), ('back34', 45), ('right', 270), ('front34r', 225)):
    B += f'set cg_thirdPersonAngle {ang}\nwait 40\nscreenshot {prefix}_idle_{name}\n'
B += 'set cg_thirdPersonRange 70\nset cg_thirdPersonVertOffset 10\nset cg_thirdPersonAngle 160\nwait 40\nscreenshot %s_closeup_face\n' % prefix
B += 'set cg_thirdPersonRange 90\nset cg_thirdPersonVertOffset -25\nset cg_thirdPersonAngle 200\nwait 40\nscreenshot %s_closeup_skirt\n' % prefix
B += 'set cg_thirdPersonRange 150\nset cg_thirdPersonVertOffset -22\nset cg_thirdPersonAngle 90\n+forward\nwait 45\nscreenshot %s_run_a\nwait 10\nscreenshot %s_run_b\nwait 10\nscreenshot %s_run_c\nwait 10\nscreenshot %s_run_d\n-forward\nwait 30\n' % ((prefix,) * 4)
B += 'setviewpos 1664 1612 -19 90\nwait 30\n+moveup\nwait 22\nscreenshot %s_jump_a\nwait 10\nscreenshot %s_jump_b\n-moveup\nwait 90\n' % ((prefix,) * 2)
B += '+attack\nwait 14\nscreenshot %s_attack_a\nwait 8\nscreenshot %s_attack_b\nwait 8\nscreenshot %s_attack_c\n-attack\nwait 30\n' % ((prefix,) * 3)
B += 'echo EVE_DONE\nquit\n'
open(os.path.join(str(REPO / '.codex-research' / 'eve2b-glm-20261012'), 'home', 'base', f'{cfgname}.cfg'), 'w').write(H + B)
