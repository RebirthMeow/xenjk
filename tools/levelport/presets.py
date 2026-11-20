"""Source-game presets: entity class and key remapping into Jedi Academy multiplayer.

A preset's entity(keys) returns the remapped keys, or None to drop the entity. worldspawn(keys) filters worldspawn.
Unknown classes pass through unchanged; the game frees what it cannot spawn.
"""


class Preset:
    arena_types = 'ffa team ctf'
    classes = {}            # source classname -> JKA classname, or None to drop
    drop_keys = ()
    worldspawn_keys = ('message', 'ambient', '_ambient', '_minlight', '_minvertexlight', '_color', 'gridsize',
                       '_blocksize', '_lightmapscale', 'fog', 'distancecull')

    def entity(self, keys):
        cls = keys.get('classname', '')
        if cls in self.classes:
            if self.classes[cls] is None:
                return None
            keys['classname'] = self.classes[cls]
        for k in self.drop_keys:
            keys.pop(k, None)
        return keys

    def worldspawn(self, keys):
        return {k: v for k, v in keys.items() if k.lower() in self.worldspawn_keys}


class NonePreset(Preset):
    pass


class QuakeLive(Preset):
    """Quake Live / Quake 3 -> JKA. Weapons by role (hitscan sniper -> disruptor, splash -> rocket launcher)."""
    classes = {
        'weapon_gauntlet': None, 'weapon_machinegun': 'weapon_blaster', 'weapon_shotgun': 'weapon_flechette',
        'weapon_grenadelauncher': 'weapon_thermal', 'weapon_rocketlauncher': 'weapon_rocket_launcher',
        'weapon_lightning': 'weapon_demp2', 'weapon_railgun': 'weapon_disruptor', 'weapon_plasmagun': 'weapon_repeater',
        'weapon_bfg': 'weapon_concussion_rifle', 'weapon_nailgun': 'weapon_bowcaster',
        'weapon_prox_launcher': 'weapon_trip_mine', 'weapon_chaingun': 'weapon_repeater', 'weapon_hmg': 'weapon_repeater',
        'ammo_bullets': 'ammo_blaster', 'ammo_shells': 'ammo_metallic_bolts', 'ammo_grenades': 'ammo_thermal',
        'ammo_rockets': 'ammo_rockets', 'ammo_lightning': 'ammo_powercell', 'ammo_slugs': 'ammo_powercell',
        'ammo_cells': 'ammo_powercell', 'ammo_bfg': 'ammo_metallic_bolts', 'ammo_nails': 'ammo_metallic_bolts',
        'ammo_mines': 'ammo_tripmine', 'ammo_belt': 'ammo_metallic_bolts', 'ammo_hmg': 'ammo_metallic_bolts',
        'ammo_pack': 'ammo_all',
        'item_armor_shard': 'item_shield_sm_instant', 'item_armor_jacket': 'item_shield_lrg_instant',
        'item_armor_combat': 'item_shield_lrg_instant', 'item_armor_body': 'item_shield_lrg_instant',
        'item_armor_green': 'item_shield_sm_instant', 'item_armor_yellow': 'item_shield_lrg_instant',
        'item_armor_red': 'item_shield_lrg_instant',
        'item_health_small': 'item_medpak_instant', 'item_health': 'item_medpak_instant',
        'item_health_large': 'item_medpak_instant', 'item_health_mega': 'item_medpak_instant',
        'item_quad': 'item_force_boon', 'item_enviro': None, 'item_haste': None, 'item_invis': 'item_cloak',
        'item_regen': None, 'item_flight': None, 'item_scout': None, 'item_guard': None, 'item_doubler': None,
        'item_ammoregen': None, 'holdable_teleporter': None, 'holdable_medkit': 'item_medpac',
        'holdable_kamikaze': None, 'holdable_portal': None, 'holdable_invulnerability': None,
        'team_CTF_neutralflag': None, 'team_redobelisk': None, 'team_blueobelisk': None, 'team_neutralobelisk': None,
        'race_point': None, 'advertisement': None, 'target_speaker': None, 'info_player_race': None,
        'misc_portal_surface': None, 'misc_portal_camera': None,
    }
    drop_keys = ('not_gametype', 'notbot', 'nohumans', 'notbots', 'cellId')
    GAMETYPES = {'tdm': 'team', 'ca': 'team', 'ft': 'team', 'ad': 'ctf', 'dom': 'team', 'harvester': 'ctf',
                 'oneflag': 'ctf', 'race': 'ffa', 'rr': 'team'}

    def entity(self, keys):
        keys = super().entity(keys)
        if keys and 'gametype' in keys:
            parts = [p.strip() for p in keys['gametype'].replace(',', ' ').split()]
            keys['gametype'] = ' '.join(sorted(set(self.GAMETYPES.get(p, p) for p in parts)))
        return keys


PRESETS = {'none': NonePreset, 'quakelive': QuakeLive}
