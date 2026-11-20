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
"""Writes tools/ui/xen_player.menu, the player screen (docs/wishlist/player-screen.md; codemp/ui/ui_player.c and
ui_player_look.c).

  python tools/ui/make_xen_player_menu.py --out tools/ui/xen_player.menu
  python tools/ui/make_xen_player_menu.py --check        (exit 1 when the tracked menu differs from the output)

The screen shows three items: the look's backdrop (ownerdraw 417), the picker's stage (401, the left 990 x 1080 of the
look's 1920 x 1080 canvas) and the look (418), which draws everything else and takes the keys. Every other item is a
stock control the look drives and never shows (ui_player.c hides them): the Saber and Force items keep the stock
ingame_saber / ingame_playerforce names, groups, feeders, ownerdraws and uiScripts, because the stock UI code finds
its items by those names (UI_UpdateSaberHilt needs the model items "saber" and "saber2", the hilt scripts
"hiltbut*"); the tab, commit and card buttons are the scripted checks' handles (ui_menuitem). The menu is generated
because its Force rows, blade chips and commit buttons repeat. Ship the output as ui/jamp/xen_player.menu (in a pack,
or loose in base/ui/jamp) with the look's art (tools/ui/bake_xen_player_art.py).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TRACKED = os.path.join(HERE, 'xen_player.menu')

ACCENT = '.549 .854 1 1'
TEXT = '.85 .85 .85 1'
DIM = '.6 .6 .6 1'
TEAM_TYPES = '"0" ; "1" ; "2" ; "3" ; "4" ; "5"'      # ui_about_gametype values without teams


def item(**kw):
    """one itemDef; values are written as given, blocks (action, mouseEnter, ...) as lists of lines"""
    out = ['\t\titemDef', '\t\t{']
    for k, v in kw.items():
        if v is None:
            continue
        if isinstance(v, list):
            out.append(f'\t\t\t{k}')
            out.append('\t\t\t{')
            out += [f'\t\t\t\t{ln}' for ln in v]
            out.append('\t\t\t}')
        elif v is True:
            out.append(f'\t\t\t{k}')
        else:
            out.append(f'\t\t\t{k:<20}\t{v}')
    out.append('\t\t}')
    return '\n'.join(out)


def text(name, txt, rect, scale='.6', color=TEXT, font='2', align='ITEM_ALIGN_LEFT', alignx='0', group=None, extra=None):
    kw = dict(name=name, group=group, style='WINDOW_STYLE_EMPTY', text=txt, rect=rect, textalign=align,
              textalignx=alignx, textaligny='0', font=font, textscale=scale, forecolor=color, visible='1')
    kw.update(extra or {})
    kw['decoration'] = True
    return item(**kw)


def button(name, txt, rect, actions, group=None, scale='.6', back='.15 .15 .15 .8', color='.8 .8 .8 1', extra=None):
    x, y, w, h = rect.split()
    kw = dict(name=name, group=group, type='ITEM_TYPE_BUTTON', style='WINDOW_STYLE_FILLED', backcolor=back, text=txt,
              rect=rect, textalign='ITEM_ALIGN_CENTER', textalignx=str(int(w) // 2), textaligny='2', font='2',
              textscale=scale, forecolor=color, visible='1')
    kw.update(extra or {})
    kw['mouseEnter'] = [f'setitemcolor {name} forecolor 1 1 1 1']
    kw['mouseExit'] = [f'setitemcolor {name} forecolor {color}']
    kw['action'] = ['play "sound/interface/button1.wav"'] + actions
    return item(**kw)


def owner(name, num, rect, scale='.55', color=TEXT, font='2', group=None, extra=None, decoration=True, action=None):
    kw = dict(name=name, group=group, ownerdraw=str(num), rect=rect, textalign='ITEM_ALIGN_LEFT', textalignx='0',
              textaligny='0', font=font, textscale=scale, forecolor=color, visible='1')
    kw.update(extra or {})
    if action:
        kw['action'] = action
    if decoration:
        kw['decoration'] = True
    return item(**kw)


def marker(name):
    return item(name=name, rect='0 0 0 0', visible='0', decoration=True)


items = []

# ---------------------------------------------------------------- the look: the only items shown
items.append(owner('xp_sections', 416, '0 0 0 0'))          # keeps the items' visibility (ui_player.c); first item
items.append(owner('xp_look_bg', 417, '0 0 640 480'))       # the backdrop (ui_player_look.c)
items.append(owner('stage', 401, '0 0 330 480', scale='.55', color='1 .682 0 1'))       # the picker's stage
items.append(owner('xp_look', 418, '0 0 640 480'))          # everything else

# ---------------------------------------------------------------- common: the scripted checks' handles
for i, word in enumerate(['Character', 'Saber', 'Force', 'Team']):
    group = 'xp_ingame' if word == 'Team' else None
    items.append(button(f'xp_tab{i}', f'"{word.upper()}"', f'{10 + i * 155} 30 150 18', [f'uiScript xpSection {i}'],
                        group=group, scale='.65', back='.1 .1 .15 .9'))

# the commit row (ui_player.c shows the buttons for the context, design 3.6)
commit = [
    ('xp_back', '"Back"', '10', ['uiScript xpBack'], 'xp_back'),
    ('xp_apply', '"Apply"', '90', ['uiScript xpCommit apply'], 'xp_apply'),
    ('xp_join', '"Join Game"', '175', ['uiScript xpCommit free'], 'xp_join'),
    ('xp_auto', '"Auto"', '175', ['uiScript xpCommit auto'], 'xp_auto'),
    ('xp_red', '"Join Red"', '260', ['uiScript xpCommit red'], 'xp_red'),
    ('xp_blue', '"Join Blue"', '345', ['uiScript xpCommit blue'], 'xp_blue'),
    ('xp_single', '"Join Single"', '260', ['uiScript xpCommit single'], 'xp_duel'),
    ('xp_double', '"Join Double"', '345', ['uiScript xpCommit double'], 'xp_duel'),
    ('xp_class', '"Choose Class"', '260', ['uiScript xpCommit class'], 'xp_class'),
    ('xp_spec', '"Spectate"', '430', ['uiScript xpCommit s'], 'xp_spec'),
]
for name, txt, x, act, group in commit:
    back = '.2 .35 .6 .9' if name in ('xp_apply', 'xp_join', 'xp_auto') else '.15 .15 .15 .8'
    items.append(button(name, txt, f'{x} 434 80 20', act, group=group, back=back))

# ---------------------------------------------------------------- Character: the picker
items.append(marker('xp_section_character'))
items.append(button('face', '"Face view (F)"', '420 334 210 20', ['uiScript pickerFace']))
items.append(button('classic', '"Classic screen"', '420 362 210 20', ['uiScript xpClassic']))

# ---------------------------------------------------------------- Saber: stock ingame_saber items
items.append(marker('xp_section_saber'))
items.append(text('typebut', '@MENUS_SABER_TYPE', '10 84 210 16', scale='.6', color=ACCENT))
types = [('typebut_single', '@MENUS_SINGLESABER', 'single', 'single_1', 'none'),
         ('typebut_dual', '@MENUS_DUALSABERS', 'dual', 'single_1', 'single_1'),
         ('typebut_staff', '@MENUS_SABERSTAFF', 'staff', 'dual_1', 'none')]
for i, (name, txt, kind, s1, s2) in enumerate(types):
    items.append(button(name, txt, f'{10 + i * 70} 102 68 18', [
        'play "sound/interface/choose_saber.wav"', f'setcvar ui_saber_type "{kind}"', 'uiScript "saber_type"',
        f'setcvar ui_saber "{s1}"', f'setcvar ui_saber2 "{s2}"', 'uiScript "saber_hilt"', 'uiScript "saber_color"',
        'uiScript "saber2_hilt"', 'uiScript "saber2_color"'], scale='.55'))


def hiltlist(name, feeder, rect, hide, script, saber_script, label=None):
    out = []
    if label:
        x, y = rect.split()[:2]
        out.append(text(f'{name}_label', label, f'{x} {int(y) - 16} 210 14', scale='.55', color=ACCENT,
                        extra={'cvarTest': 'ui_saber_type', 'hideCvar': f'{{ {hide} }}'}))
    out.append(item(name=name, rect=rect, type='ITEM_TYPE_LISTBOX', style='WINDOW_STYLE_FILLED', elementwidth='190',
                    elementheight='16', font='2', textscale='.6', textaligny='14', border='1', bordersize='1',
                    bordercolor='.3 .3 .5 1', forecolor='.8 .8 1 1', backcolor='0 0 .2 .4', outlinecolor='.25 .45 .75 .7',
                    elementtype='LISTBOX_TEXT', feeder=feeder, cvarTest='ui_saber_type', hideCvar=f'{{ {hide} }}',
                    visible='1', action=['play "sound/interface/choose_hilt.wav"', f'uiScript "{script}"',
                                         f'uiScript "{saber_script}"']))
    return out


items += hiltlist('hiltbut', 'FEEDER_SABER_SINGLE_INFO', '10 142 210 266', '"staff" ; "dual"', 'setscreensaberhilt', 'saber_hilt',
                  label='@MENUS_HILT1')
items += hiltlist('hiltbut_staves', 'FEEDER_SABER_STAFF_INFO', '10 142 210 266', '"single" ; "dual"', 'setscreensaberstaff',
                  'saber_hilt')
items += hiltlist('hiltbut1', 'FEEDER_SABER_SINGLE_INFO', '10 142 210 120', '"single" ; "staff"', 'setscreensaberhilt1',
                  'saber_hilt', label='@MENUS_HILT1')
items += hiltlist('hiltbut2', 'FEEDER_SABER_SINGLE_INFO', '10 288 210 120', '"single" ; "staff"', 'setscreensaberhilt2',
                  'saber2_hilt', label='@MENUS_HILT2')
COLORS = ['blue', 'green', 'orange', 'purple', 'yellow', 'red']
for blade, cvar, y, hide in ((1, 'ui_saber_color', 100, None), (2, 'ui_saber2_color', 150, '"single" ; "staff"')):
    title = '@MENUS_BLADE_COLOR' if blade == 1 else '@MENUS_COLOR2'
    test = {'cvarTest': 'ui_saber_type', 'hideCvar': f'{{ {hide} }}'} if hide else {}
    items.append(text(f'bladecolortitle{blade}', title, f'420 {y - 16} 210 14', scale='.55', color=ACCENT, extra=test))
    for i, c in enumerate(COLORS):
        name = f'{c}icon' + ('2' if blade == 2 else '')
        kw = dict(name=name, group=f'sabericons{"2" if blade == 2 else ""}', style='WINDOW_STYLE_SHADER',
                  type='ITEM_TYPE_BUTTON', rect=f'{420 + i * 30} {y} 24 24', background=f'"gfx/menus/saber_icon_{c}"',
                  forecolor='.75 .75 .75 1', border='1', bordersize='1', bordercolor='.33 .33 .5 1', visible='1')
        kw.update(test)
        kw['mouseEnter'] = [f'setitemcolor {name} bordercolor .66 .66 1 1']
        kw['mouseExit'] = [f'setitemcolor {name} bordercolor .33 .33 .5 1']
        kw['action'] = ['play "sound/interface/choose_blade.wav"', f'setcvar {cvar} "{c}"']
        items.append(item(**kw))
# the hilt previews (stock's saber views; UI_UpdateSaberHilt needs both items)
for name, flag, y, hide in (('saber', 'isSaber', 190, None), ('saber2', 'isSaber2', 290, '"single" ; "staff"')):
    kw = dict(name=name, group='models', type='ITEM_TYPE_MODEL', rect=f'420 {y} 210 100',
              asset_model='"models/weapons2/saber_reborn/saber_w.glm"', model_angle='180', model_rotation='20',
              model_g2mins='0 0 0', model_g2maxs='20 20 20', model_fovx='75', model_fovy='75', visible='1')
    kw[flag] = '1'
    if hide:
        kw.update({'cvarTest': 'ui_saber_type', 'hideCvar': f'{{ {hide} }}'})
    kw['decoration'] = True
    items.append(item(**kw))

# ---------------------------------------------------------------- Force: stock ingame_playerforce items
items.append(marker('xp_section_force'))
POWERS = [
    ('neutral', None, [('setfp_jump', '@MENUS_FORCE_JUMP', 'UI_FORCE_RANK_LEVITATION'),
                       ('setfp_push', '@MENUS_FORCE_PUSH', 'UI_FORCE_RANK_PUSH'),
                       ('setfp_pull', '@MENUS_FORCE_PULL', 'UI_FORCE_RANK_PULL'),
                       ('setfp_speed', '@MENUS_FORCE_SPEED', 'UI_FORCE_RANK_SPEED'),
                       ('setfp_see', '@MENUS_FORCE_SIGHT', 'UI_FORCE_RANK_SEE')]),
    ('light', 'lightpowers', [('setfp_absorb', '@MENUS_FORCE_ABSORB', 'UI_FORCE_RANK_ABSORB'),
                              ('setfp_healself', '@MENUS_FORCE_HEAL', 'UI_FORCE_RANK_HEAL'),
                              ('setfp_protect', '@MENUS_FORCE_PROTECT', 'UI_FORCE_RANK_PROTECT'),
                              ('setfp_mindtrick', '@MENUS_FORCE_MINDTRICK', 'UI_FORCE_RANK_TELEPATHY'),
                              ('setfp_teamheal', '@MENUS_FORCE_TEAM_HEAL', 'UI_FORCE_RANK_TEAM_HEAL')]),
    ('dark', 'darkpowers', [('setfp_grip', '@MENUS_FORCE_GRIP', 'UI_FORCE_RANK_GRIP'),
                            ('setfp_drain', '@MENUS_FORCE_DRAIN', 'UI_FORCE_RANK_DRAIN'),
                            ('setfp_lightning', '@MENUS_FORCE_LIGHTNING', 'UI_FORCE_RANK_LIGHTNING'),
                            ('setfp_rage', '@MENUS_DARK_RAGE', 'UI_FORCE_RANK_RAGE'),
                            ('setfp_powerother', '@MENUS_FORCE_TEAM_REPLENISH', 'UI_FORCE_RANK_TEAM_FORCE')]),
    ('saber', None, [('setfp_saberattack', '@MENUS_SABER_ATTACK', 'UI_FORCE_RANK_SABERATTACK'),
                     ('setfp_saberdefend', '@MENUS_SABER_DEFEND', 'UI_FORCE_RANK_SABERDEFEND'),
                     ('setfp_saberthrow', '@MENUS_SABER_THROW', 'UI_FORCE_RANK_SABERTHROW')]),
]
y = 86
for kind, group, rows in POWERS:
    head = {'neutral': '"Neutral"', 'light': '"Light side"', 'dark': '"Dark side"', 'saber': '"Saber"'}[kind]
    if kind == 'dark':
        y = light_y          # the dark rows share the light rows' place: one side shows
    else:
        items.append(text(f'xp_force_{kind}', head, f'10 {y} 210 12', scale='.5', color=ACCENT, group=group or 'yesforce'))
    if kind == 'light':
        light_y = y
    if kind == 'dark':
        items.append(text(f'xp_force_{kind}', head, f'10 {y} 210 12', scale='.5', color=ACCENT, group=group))
    y += 14
    for name, label, od in rows:
        team = name in ('setfp_teamheal', 'setfp_powerother')
        g = (group + '_team') if team else (group or 'yesforce')
        test = {'cvarTest': '"ui_about_gametype"', 'hideCvar': f'{{ {TEAM_TYPES} }}'} if team else {}
        items.append(text(name, label, f'10 {y} 110 15', scale='.55', color=TEXT, align='ITEM_ALIGN_RIGHT', alignx='105',
                          group=g, extra=test))
        kw = dict(name=name, group=g, style='0', ownerdraw=od, rect=f'10 {y} 175 15', textalign='ITEM_ALIGN_LEFT',
                  textalignx='115', textaligny='-6', textscale='.25', forecolor='.8 .8 .8 1', visible='1')
        kw.update(test)
        kw['mouseEnter'] = [f'setitemcolor {name} forecolor 1 1 1 1']
        kw['mouseExit'] = [f'setitemcolor {name} forecolor .8 .8 .8 1']
        kw['action'] = ['play "sound/interface/button1.wav"']
        items.append(item(**kw))
        y += 16
    y += 4
# the right rail: rank, side, points, Jedi / Non-Jedi, templates
items.append(owner('siderank', 'UI_FORCE_RANK', '420 84 210 16', scale='.6', group='yesforce', extra={'text': '@MENUS_FORCE_MASTERY'}))
items.append(owner('setside', 'UI_FORCE_SIDE', '420 104 210 16', scale='.6', group='yesforce', decoration=False,
                   extra={'text': '@MENUS_FORCE_SIDE'}, action=['play "sound/interface/button1.wav"']))
items.append(owner('setpoints', 'UI_FORCE_POINTS', '420 124 210 16', scale='.6', group='yesforce',
                   extra={'text': '@MENUS_POINTS_REMAINING'}))
items.append(owner('jedinonjedi', 'UI_JEDI_NONJEDI', '420 144 210 16', scale='.6', group='jedinonjedi', decoration=False,
                   extra={'text': '@MENUS_JEDI_NONJEDI'}, action=['uiScript updateForceStatus']))
items.append(text('fcflist', '@MENUS_FORCE_TEMPLATES', '420 166 210 14', scale='.55', color=ACCENT, group='yesforce'))
items.append(item(name='fcflist', group='yesforce', rect='420 182 210 120', type='ITEM_TYPE_LISTBOX',
                  style='WINDOW_STYLE_FILLED', elementwidth='190', elementheight='16', font='2', textscale='.6',
                  elementtype='LISTBOX_TEXT', feeder='FEEDER_FORCECFG', textaligny='14', border='1', bordersize='1',
                  bordercolor='.3 .3 .5 1', forecolor='.8 .8 1 1', backcolor='0 0 .2 .4', outlinecolor='.25 .45 .75 .7',
                  visible='1', action=['play "sound/interface/button1.wav"']))
items.append(item(name='savefield', group='yesforce', type='ITEM_TYPE_EDITFIELD', style='WINDOW_STYLE_FILLED',
                  text='""', cvar='"ui_SaveFCF"', maxChars='12', maxPaintChars='12', rect='420 312 130 18',
                  textalign='ITEM_ALIGN_LEFT', textalignx='4', textaligny='0', font='2', textscale='.6',
                  forecolor='1 1 1 1', backcolor='.1 .1 .15 .9', visible='1'))
items.append(button('templatesave', '@MENUS_SAVE_FILE', '555 312 75 18', ['uiScript saveTemplate'], group='yesforce'))

# ---------------------------------------------------------------- Team: one card per choice
items.append(marker('xp_section_team'))
cards = [('xp_card_auto', '"Auto: the server picks"', 'auto', 'xp_auto'),
         ('xp_card_join', '"Join Game"', 'free', 'xp_join'),
         ('xp_card_red', '"Red team"', 'red', 'xp_red'),
         ('xp_card_blue', '"Blue team"', 'blue', 'xp_blue'),
         ('xp_card_single', '"Single (Power Duel)"', 'single', 'xp_duel'),
         ('xp_card_double', '"Double (Power Duel)"', 'double', 'xp_duel'),
         ('xp_card_class', '"Choose a Siege class"', 'class', 'xp_class'),
         ('xp_card_spec', '"Spectate"', 's', 'xp_spec')]
for i, (name, txt, what, group) in enumerate(cards):
    items.append(button(name, txt, f'10 {86 + i * 30} 210 24', [f'uiScript xpCommit {what}'], group=group, scale='.65'))

items.append(marker('xp_section_common'))

menu = f'''//------------------------------------------------------------------------------------------------
// xen_player: the player screen (codemp/ui/ui_player.c, ui_player_look.c; docs/wishlist/player-screen.md)
//
// Written by tools/ui/make_xen_player_menu.py: edit that, not this. Opens in place of the stock player screens
// (ingame_player, playerMenu, ingame_saber, saberMenu, ingame_playerforce, ingame_join) while ui_playerScreen is 1.
// Shown: the backdrop, the stage and the look; every other item is a stock control the look drives (hidden).
//------------------------------------------------------------------------------------------------
{{
	menuDef
	{{
		name						"xen_player"
		visible						0
		fullscreen					0
		rect						0 0 640 480
		style						WINDOW_STYLE_EMPTY
		backcolor					0 0 0 0
		focusColor					1 1 1 1
		onOpen
		{{
			uiScript				update "ui_GetName"
			uiScript				updateForceStatus
			uiScript				getsabercvars
			uiScript				getsaberhiltinfo
			uiScript				saber_hilt
			uiScript				saber_color
			uiScript				saber2_hilt
			uiScript				saber2_color
			uiScript				pickerOpen
			uiScript				xpOpen
		}}
		onClose
		{{
			uiScript				pickerClose
		}}
		onESC
		{{
			play					"sound/interface/esc.wav"
			uiScript				xpBack
		}}

{chr(10).join(items)}
	}}
}}
'''


def main(argv=None):
    ap = argparse.ArgumentParser(description='Write the player screen menu (xen_player.menu).')
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument('--out', help='the menu file to write (the tracked one is tools/ui/xen_player.menu)')
    mode.add_argument('--check', action='store_true',
                      help='write nothing; exit 1 when tools/ui/xen_player.menu differs from the output')
    a = ap.parse_args(argv)
    data = menu.replace('\n', '\r\n').encode('latin-1')
    if a.check:
        with open(TRACKED, 'rb') as f:
            same = f.read() == data
        print(TRACKED, 'matches the generator' if same else 'differs from the generator: run with --out')
        return 0 if same else 1
    with open(a.out, 'wb') as f:
        f.write(data)
    print(a.out, len(items), 'items')
    return 0


if __name__ == '__main__':
    sys.exit(main())
