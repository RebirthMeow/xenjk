import json
import os
import tempfile
import unittest

from laps_compare import world_metrics

COLUMNS = 'level_time,client,team,captures'


def make_world(root, clients, items, captures, summary):
    """clients: {id: (name, team 1 red / 2 blue)}; items: [(client, flag)]; captures: {id: final count}."""
    base = os.path.join(root, 'live', 'server', 'base')
    os.makedirs(os.path.join(base, 'bot2-play'))
    lines = ['0:01 ClientConnect: %d [Bot] (BOT) "%s^7"' % (c, n) for c, (n, t) in clients.items()]
    lines += ['0:%02d Item: %d team_CTF_%sflag' % (10 + i, c, f) for i, (c, f) in enumerate(items)]
    with open(os.path.join(base, 'play-game.log'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.join(base, 'bot2-play', 'w.csv'), 'w', encoding='utf-8') as f:
        f.write(COLUMNS + '\n')
        for c, (n, t) in clients.items():
            f.write('1000,%d,%d,0\n' % (c, t))
            f.write('600000,%d,%d,%d\n' % (c, t, captures.get(c, 0)))
    with open(os.path.join(root, 'summary.json'), 'w', encoding='utf-8') as f:
        json.dump(dict(summary, game_seconds=600), f)
    return root


class CaptureMetricTests(unittest.TestCase):
    def test_two_sided_counts_captures_not_returns(self):
        clients = {0: ('Runner_v2', 2), 1: ('Defender_v2', 2), 3: ('Camper_v2', 2),
                   4: ('Runner2_v2', 1), 5: ('Defender2_v2', 1), 7: ('Camper2_v2', 1)}
        # Blue runner: picks up red, captures (touches blue). Blue camper: returns blue twice.
        # Red runner: picks up blue, captures (touches red). Red defender returns red once.
        items = [(0, 'red'), (0, 'blue'), (3, 'blue'), (3, 'blue'), (4, 'blue'), (4, 'red'), (5, 'red')]
        with tempfile.TemporaryDirectory() as d:
            make_world(d, clients, items, {0: 1, 4: 1},
                       dict(team_captures={'Runner_v2': 1, 'Camper_v2': 2, 'Runner2_v2': 1},
                            red_flag_pickups={'Runner_v2': 1, 'Runner2_v2': 1, 'Defender2_v2': 1},
                            captures_by_team={'red': 1, 'blue': 1}))
            m = world_metrics(d)
        self.assertEqual(m['team_caps'], 1.0)      # (blue 1 + red 1) / 2 teams
        self.assertEqual(m['runner_caps'], 1.0)
        self.assertEqual(m['camper_caps'], 0.0)    # the camper's touches were returns
        self.assertEqual(m['pickups'], 1.0)        # one enemy-flag pickup a team
        self.assertEqual(m['team_returns'], 1.5)   # blue 2 + red 1, a team
        self.assertEqual(m['all_caps'], 2.0)

    def test_one_sided_keeps_touches_as_captures(self):
        clients = {0: ('Runner_v2', 2), 1: ('Defender_v2', 1), 3: ('Camper_v2', 2)}
        items = [(0, 'red'), (0, 'blue'), (3, 'red'), (3, 'blue')]
        with tempfile.TemporaryDirectory() as d:
            make_world(d, clients, items, {0: 1, 3: 1},
                       dict(team_captures={'Runner_v2': 1, 'Camper_v2': 1},
                            red_flag_pickups={'Runner_v2': 1, 'Camper_v2': 1}))
            m = world_metrics(d)
        self.assertEqual((m['team_caps'], m['runner_caps'], m['camper_caps'], m['pickups']), (2.0, 1.0, 1.0, 2.0))
        self.assertNotEqual(m['team_returns'], m['team_returns'])   # NaN: no such metric one-sided

    def test_defender_deaths_both_teams_and_runner_self_kills(self):
        # The blue declared defenders count (mean of the two teams), self-kills are not defender deaths,
        # runner self-kills (MOD_SUICIDE) have their own metric, and only the red base and chase D's touches of
        # the blue flag are defender_enemy_flag.
        clients = {0: ('Runner_v2', 2), 1: ('Defender_v2', 1), 2: ('Chaser_v2', 1), 4: ('Runner2_v2', 1),
                   5: ('Defender2_v2', 2), 6: ('Chaser2_v2', 2)}
        with tempfile.TemporaryDirectory() as d:
            make_world(d, clients, [(1, 'blue'), (5, 'blue'), (4, 'blue')], {},
                       dict(team_captures={'Defender_v2': 1, 'Defender2_v2': 1, 'Runner2_v2': 1},
                            runner_deaths={'MOD_SUICIDE': 2}, runner2_deaths={'MOD_SUICIDE': 1}))
            with open(os.path.join(d, 'live', 'server', 'base', 'play-game.log'), 'a', encoding='utf-8') as f:
                f.write('0:30 Kill: 0 1 19: Runner_v2 killed Defender_v2 by MOD_ROCKET\n'
                        '0:31 Kill: 4 5 19: Runner2_v2 killed Defender2_v2 by MOD_ROCKET\n'
                        '0:32 Kill: 4 6 19: Runner2_v2 killed Chaser2_v2 by MOD_SABER\n'
                        '0:33 Kill: 2 2 37: Chaser_v2 killed Chaser_v2 by MOD_SUICIDE\n')
            m = world_metrics(d)
        self.assertEqual(m['defender_deaths'], 1.5)       # red 1 + blue 2, a team; the self-kill left out
        self.assertEqual(m['runners_self_kills'], 3.0)
        self.assertEqual(m['defender_enemy_flag'], 1.0)   # Defender_v2's touch only


if __name__ == '__main__':
    unittest.main()
