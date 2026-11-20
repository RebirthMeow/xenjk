"""The run tools/bike/wheel_gate.py checks (a front tyre that is not drawn fails it): the bike parked and ridden on
mp/ffa3 from the side, labelled screenshots (parked with the front suspension bone driven, cg_bikeFrontArm 1, and not,
0; riding), cg_bikeSeatDebug 250 for the BIKESCREEN and BIKEARM lines. The installed x86 client on localhost with a
private home holding the modules of BIKE_BUILD and the pack BIKE_PACK (the installed GameData is never touched), via
the client-check harness. Run inside the game lock:
  python -I <game lock script> python -I wheel_check.py RUN_DIR
then: python -I wheel_gate.py REFERENCE_RUN RUN_DIR

Environment: BIKE_BUILD (required), BIKE_PACK, XENJK_MAIN (the checkout holding .codex-research; default this
repository), CLIENT_CHECKS (the client-check harness folder with cc.py; not in this repository).
"""
import os
import shutil
import sys

MAIN = os.environ.get('XENJK_MAIN', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.environ.get('CLIENT_CHECKS', os.path.join(MAIN, '.codex-research', 'client-checks-20261014-a')))
import cc  # noqa: E402

BUILD = os.environ.get('BIKE_BUILD')   # the Release folder with jampgamex86.dll and cgamex86.dll
PACK = os.environ.get('BIKE_PACK', os.path.join(MAIN, '.codex-research', 'wishlist-20261101', 'drafts', 'bike', 'game',
                                                'zz_ct3x_bike_v3.pk3'))
SHOT = ['screenshot', 'wait 160']


def main(tag):
    if not BUILD:
        raise SystemExit('set BIKE_BUILD to the Release folder with jampgamex86.dll and cgamex86.dll')
    run = os.path.abspath(tag)
    base = os.path.join(run, 'home_rider', 'base')
    os.makedirs(base, exist_ok=True)
    for f in ('jampgamex86.dll', 'cgamex86.dll'):
        shutil.copy2(os.path.join(BUILD, f), os.path.join(base, f))
    shutil.copy2(PACK, os.path.join(base, 'zz_ct3x_bike.pk3'))
    boot = ['set g_bikeDebug 0', 'set cg_bikeSeatDebug 250', 'set developer 1', 'set g_gametype 0', 'set cg_thirdPerson 1',
            'set bot_enable 0', 'set cg_drawGun 0', 'set cg_draw2D 0', 'devmap mp/ffa3']
    c = cc.Client(run, 'rider', ['+set', 'in_mouse', '0', '+set', 'net_enabled', '1'], boot)
    cc.CLIENTS.append(c)
    try:
        c.step(['wait 300', 'team free', 'wait 100'], timeout=240)
        cc.wait(2)
        c.escape()
        c.step(['wait 50', 'togglemenu', 'wait 20', 'togglemenu', 'wait 50', 'setviewpos 2432 -800 -20 90', 'wait 100',
                'npc spawn vehicle ct3x', 'wait 200', 'bikeboard', 'wait 400', 'set cg_thirdPersonAngle 90', 'wait 60'])
        c.step(['echo SHOT parked_arm1'] + SHOT + ['set cg_bikeFrontArm 0', 'wait 30', 'echo SHOT parked_arm0'] + SHOT
               + ['set cg_bikeFrontArm 1', 'wait 30'])
        c.step(['+forward', 'wait 60', 'echo SHOT ride_arm1', 'screenshot', 'wait 40', '-forward', '+back', 'wait 120',
                '-back', 'wait 60'])
        c.step(['quit'], wait=False)
        cc.wait(5)
    finally:
        c.close(run)


if __name__ == '__main__':
    main(sys.argv[1])
