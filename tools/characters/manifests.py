"""manifests.py: character.cfg for every model folder the picker should group or name (codemp/ui/ui_roster.c).

    python tools/characters/manifests.py DIR            # DIR/<folder>/character.cfg
    python tools/characters/manifests.py --pack OUT.pk3 # the same files as a pack (never rebuilt in place), with the
                                                        # picker screen ui/jamp/xen_character.menu (xen_character.menu here)

The table below is the roster's data: our converts (the folders the short-names pack lists) and the stock JKA
folders. Folders not in it (other mods) group by name in the UI. Variant names marked "folder code" keep the
convert's own suffix where its meaning is not recorded; rename them here.

Then check the result against the installed folders:

    python tools/characters/roster_check.py --manifests DIR
"""
import argparse
import sys
import zipfile
from pathlib import Path

# folder: (character, character name, variant name, source, character order, variant order)
FF7R, CP, SB, JKA = 'ff7r', 'cp2077', 'stellar', 'jka'
# source tag: (tab label or None for the tag in capitals, the details line's name, the tab's place). The picker builds
# its tabs from these (codemp/ui/ui_roster.c); jka (the stock models) and other are built in and come last.
SOURCES = {
    FF7R: (None, 'Final Fantasy VII Remake / Rebirth', 10),
    CP: (None, 'Cyberpunk 2077', 20),
    SB: (None, 'Stellar Blade', 30),
    'rivals': (None, 'Marvel Rivals', 40),
}
TABLE = {
    # FF7 Remake / Rebirth
    'tifa': ('tifa', 'Tifa', 'Original', FF7R, 10, 0),
    'tifa_noglove': ('tifa', 'Tifa', 'No Gloves', FF7R, 10, 10),
    'tifa_costa': ('tifa', 'Tifa', 'Costa del Sol', FF7R, 10, 20),
    'tifa_costa2': ('tifa', 'Tifa', 'Costa del Sol 2', FF7R, 10, 21),
    'tifa_tux': ('tifa', 'Tifa', 'Tux', FF7R, 10, 30),
    'tifa_witchy': ('tifa', 'Tifa', 'Witchy', FF7R, 10, 40),
    'tifa_sylus': ('tifa', 'Tifa', 'Sylus', FF7R, 10, 50),
    'aerith': ('aerith', 'Aerith', 'Original', FF7R, 20, 0),
    'aerith_nojacket': ('aerith', 'Aerith', 'No Jacket', FF7R, 20, 10),
    'aerith_costa': ('aerith', 'Aerith', 'Costa del Sol', FF7R, 20, 20),
    'aerith_costa2': ('aerith', 'Aerith', 'Costa del Sol 2', FF7R, 20, 21),
    'aerith_loveless': ('aerith', 'Aerith', 'Loveless', FF7R, 20, 30),
    'aerith_tux': ('aerith', 'Aerith', 'Tux', FF7R, 20, 40),
    'aerith_xavier': ('aerith', 'Aerith', 'Xavier', FF7R, 20, 50),
    'aerith_zayne': ('aerith', 'Aerith', 'Zayne', FF7R, 20, 51),
    'aerith_sylus': ('aerith', 'Aerith', 'Sylus', FF7R, 20, 52),
    'aerith_rafayel': ('aerith', 'Aerith', 'Rafayel', FF7R, 20, 53),
    'gentiana': ('gentiana', 'Gentiana', 'Original', FF7R, 30, 0),
    'vincent': ('vincent', 'Vincent', 'Original', FF7R, 40, 0),
    'amina': ('amina', 'Amina', 'Original', FF7R, 50, 0),
    'fren': ('fran', 'Fran', 'Original', FF7R, 60, 0),
    'fren_fashion': ('fran', 'Fran', 'Fashion', FF7R, 60, 10),
    'junon_card': ('isabelle', 'Isabelle', 'Original', FF7R, 70, 0),
    # Stellar Blade
    'eve': ('eve', 'Eve', 'Original', SB, 100, 0),
    'eve_skinsuit': ('eve', 'Eve', 'Skin Suit', SB, 100, 10),
    'eve_kunoichi': ('eve', 'Eve', 'White Kunoichi', SB, 100, 20),
    'eve_14': ('eve', 'Eve', 'Outfit 14', SB, 100, 30),           # folder code (EVE_14)
    'eve_16': ('eve', 'Eve', 'Outfit 16', SB, 100, 31),           # folder code (EVE_16, the bob)
    'eve2b_nb': ('eve', 'Eve', '2B, No Blindfold', SB, 100, 41),
    'tachy': ('tachy', 'Tachy', 'Original', SB, 110, 0),
    # Cyberpunk 2077
    'cp_judy': ('judy', 'Judy', 'Original', CP, 200, 0),
    'cp_judy_2yl': ('judy', 'Judy', '2YL', CP, 200, 10),          # folder code
    'cp_panam': ('panam', 'Panam', 'Original', CP, 210, 0),
    'cp_panam_noharness': ('panam', 'Panam', 'No Harness', CP, 210, 10),
    'cp_songbird': ('songbird', 'Songbird', 'Original', CP, 220, 0),
    'cp_songbird_jacket': ('songbird', 'Songbird', 'Jacket', CP, 220, 10),
    'cp_songbird_paradise': ('songbird', 'Songbird', 'Paradise', CP, 220, 20),
    'cp_hanako': ('hanako', 'Hanako', 'Original', CP, 230, 0),
    'cp_hanako_parade': ('hanako', 'Hanako', 'Parade', CP, 230, 10),
    'cp_johnny': ('johnny', 'Johnny', 'Original', CP, 240, 0),
    'cp_alt': ('alt', 'Alt', 'Original', CP, 250, 0),
    'cp_alt_u2': ('alt', 'Alt', 'U2', CP, 250, 10),               # folder code
    'cp_evelyn': ('evelyn', 'Evelyn', 'Original', CP, 260, 0),
    'cp_evelyn_bd': ('evelyn', 'Evelyn', 'BD', CP, 260, 10),      # folder code
    'cp_peralez': ('peralez', 'Peralez', 'Original', CP, 270, 0),
    'cp_peralez_incog': ('peralez', 'Peralez', 'Incognito', CP, 270, 10),
    'cp_maiko': ('maiko', 'Maiko', 'Original', CP, 280, 0),
    'cp_maiko_casual': ('maiko', 'Maiko', 'Casual', CP, 280, 10),
    'cp_chloe_dressed': ('chloe', 'Chloe', 'Dressed', CP, 290, 0),
    'rita': ('rita', 'Rita', 'Original', CP, 300, 0),
    'garry': ('garry', 'Garry', 'Original', CP, 310, 0),
    # Marvel Rivals
    'psylocke': ('psylocke', 'Psylocke', 'Original', 'rivals', 400, 0),
    # Jedi Academy (stock folders, and Jan's HD remake)
    'kyle': ('kyle', 'Kyle Katarn', 'Original', JKA, 1000, 0),
    'jan': ('jan', 'Jan Ors', 'Original', JKA, 1000, 0),
    'jan_hd': ('jan', 'Jan Ors', 'HD', JKA, 1000, 10),
    'luke': ('luke', 'Luke Skywalker', 'Original', JKA, 1000, 0),
    'lando': ('lando', 'Lando Calrissian', 'Original', JKA, 1000, 0),
    'rosh_penin': ('rosh_penin', 'Rosh Penin', 'Original', JKA, 1000, 0),
    'tavion': ('tavion', 'Tavion', 'Original', JKA, 1000, 0),
    'tavion_new': ('tavion', 'Tavion', 'Academy', JKA, 1000, 10),
    'desann': ('desann', 'Desann', 'Original', JKA, 1000, 0),
    'alora': ('alora', 'Alora', 'Original', JKA, 1000, 0),
    'alora2': ('alora', 'Alora', 'Alora 2', JKA, 1000, 10),
    'reborn': ('reborn', 'Reborn', 'Original', JKA, 1000, 0),
    'reborn_new': ('reborn', 'Reborn', 'New', JKA, 1000, 10),
    'reborn_twin': ('reborn', 'Reborn', 'Twin', JKA, 1000, 20),
    'morgan': ('morgan', 'Morgan Katarn', 'Original', JKA, 1000, 0),
    'monmothma': ('monmothma', 'Mon Mothma', 'Original', JKA, 1000, 0),
    'jeditrainer': ('jeditrainer', 'Jedi Trainer', 'Original', JKA, 1000, 0),
    'jedi': ('jedi', 'Jedi', 'Original', JKA, 1000, 0),
    'jedi_hf': ('jedi', 'Jedi', 'Human Female', JKA, 1000, 10),
    'jedi_hm': ('jedi', 'Jedi', 'Human Male', JKA, 1000, 11),
    'jedi_kdm': ('jedi', 'Jedi', 'Kel Dor', JKA, 1000, 12),
    'jedi_rm': ('jedi', 'Jedi', 'Rodian', JKA, 1000, 13),
    'jedi_tf': ('jedi', 'Jedi', "Twi'lek", JKA, 1000, 14),
    'jedi_zf': ('jedi', 'Jedi', 'Zabrak', JKA, 1000, 15),
    'boba_fett': ('boba_fett', 'Boba Fett', 'Original', JKA, 1000, 0),
    'chewbacca': ('chewbacca', 'Chewbacca', 'Original', JKA, 1000, 0),
    'galak': ('galak', 'Galak', 'Original', JKA, 1000, 0),
    'rax_joris': ('rax_joris', 'Rax Joris', 'Original', JKA, 1000, 0),
    'human_merc': ('human_merc', 'Mercenary', 'Original', JKA, 1000, 0),
    'imperial': ('imperial', 'Imperial', 'Original', JKA, 1000, 0),
    'imperial_worker': ('imperial_worker', 'Imperial Worker', 'Original', JKA, 1000, 0),
    'rebel': ('rebel', 'Rebel', 'Original', JKA, 1000, 0),
    'rebel_pilot': ('rebel_pilot', 'Rebel Pilot', 'Original', JKA, 1000, 0),
    'bespin_cop': ('bespin_cop', 'Bespin Cop', 'Original', JKA, 1000, 0),
    'prisoner': ('prisoner', 'Prisoner', 'Original', JKA, 1000, 0),
    'cultist': ('cultist', 'Cultist', 'Original', JKA, 1000, 0),
    'saboteur': ('saboteur', 'Saboteur', 'Original', JKA, 1000, 0),
    'shadowtrooper': ('shadowtrooper', 'Shadowtrooper', 'Original', JKA, 1000, 0),
    'stormtrooper': ('stormtrooper', 'Stormtrooper', 'Original', JKA, 1000, 0),
    'stormpilot': ('stormpilot', 'Storm Pilot', 'Original', JKA, 1000, 0),
    'snowtrooper': ('snowtrooper', 'Snowtrooper', 'Original', JKA, 1000, 0),
    'swamptrooper': ('swamptrooper', 'Swamptrooper', 'Original', JKA, 1000, 0),
    'chiss': ('chiss', 'Chiss', 'Original', JKA, 1000, 0),
    'gran': ('gran', 'Gran', 'Original', JKA, 1000, 0),
    'noghri': ('noghri', 'Noghri', 'Original', JKA, 1000, 0),
    'reelo': ('reelo', 'Reelo', 'Original', JKA, 1000, 0),
    'rodian': ('rodian', 'Rodian', 'Original', JKA, 1000, 0),
    'trandoshan': ('trandoshan', 'Trandoshan', 'Original', JKA, 1000, 0),
    'tusken': ('tusken', 'Tusken Raider', 'Original', JKA, 1000, 0),
    'ugnaught': ('ugnaught', 'Ugnaught', 'Original', JKA, 1000, 0),
    'weequay': ('weequay', 'Weequay', 'Original', JKA, 1000, 0),
}


def cfg_text(folder):
    character, name, variant, source, order, vorder = TABLE[folder]
    lines = ['// %s: the character picker roster (tools/characters/manifests.py)' % folder,
             'character\t%s' % character, 'name\t\t"%s"' % name, 'variant\t\t"%s"' % variant,
             'source\t\t%s' % source, 'order\t\t%d' % order, 'variantorder\t%d' % vorder]
    if source in SOURCES:
        label, sname, sorder = SOURCES[source]
        if label:
            lines.append('sourcelabel\t"%s"' % label)
        lines += ['sourcename\t"%s"' % sname, 'sourceorder\t%d' % sorder]
    return '\r\n'.join(lines) + '\r\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('dir', nargs='?', type=Path)
    ap.add_argument('--pack', type=Path)
    a = ap.parse_args()
    if not a.dir and not a.pack:
        ap.error('give DIR or --pack')
    for folder, row in TABLE.items():
        assert folder == folder.lower() and len(row) == 6, folder
        assert '"' not in row[1] + row[2], folder
    if a.dir:
        for folder in TABLE:
            d = a.dir / folder
            d.mkdir(parents=True, exist_ok=True)
            (d / 'character.cfg').write_bytes(cfg_text(folder).encode('utf-8'))
        print('wrote %d character.cfg under %s' % (len(TABLE), a.dir))
    if a.pack:
        assert not a.pack.exists(), 'never rebuild a pk3 in place: bump the name'
        menu = Path(__file__).resolve().parent / 'xen_character.menu'
        with zipfile.ZipFile(a.pack, 'w', zipfile.ZIP_DEFLATED) as z:
            for folder in TABLE:
                z.writestr('models/players/%s/character.cfg' % folder, cfg_text(folder).encode('utf-8'))
            z.writestr('ui/jamp/xen_character.menu', menu.read_bytes())
        print('wrote %s: %d character.cfg and the picker menu' % (a.pack, len(TABLE)))


if __name__ == '__main__':
    sys.exit(main())
