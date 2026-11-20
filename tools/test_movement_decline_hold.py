"""Source checks for two changes in `codemp/game/bot2_move.c`.

- A walk decline the movement owner can resolve is held under that owner,
  never handed to the legacy owner: `Move_Command` asks `DeclineHeld` on every
  frame the walk declined, and `DeclineHeld` holds the walk's own declines -
  corridor, no path, miss - and nothing else. The native regression is the
  class-50 respawn4 trip, recorded under `docs/movement/records/`.
- The installed route's edges are printed as `[ROUTE]` lines, and never from
  inside a search: `RouteLine` is called from `Replan` and from the pending
  print at the end of the Pmove boundary, not from the searches or from
  `RerouteInstall`, which a commit calls inside the search.

Compilation cannot see either property, so the text is checked.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'codemp/game/bot2_move.c'


def definition(source, name):
    match = re.search(r'^(?:static )?[^\n;]*\b' + re.escape(name) + r'\([^\n;]*\)\s*\{', source, re.M)
    if not match:
        raise ValueError(f'Missing production function {name}')
    end, depth = match.end(), 1
    while depth:
        depth += {'{': 1, '}': -1}.get(source[end], 0)
        end += 1
    return source[match.start():end]


class DeclineHold(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text(encoding='utf-8')

    def test_move_command_holds_before_it_declines(self):
        body = definition(self.source, 'Move_Command')
        tail = body[body.index('PadObserve(client,walked);'):]
        self.assertIn('DeclineHeld(client,reason,&step)', tail)
        self.assertLess(tail.index('DeclineHeld('), tail.index('return walked?qtrue:qfalse;'))

    def test_only_the_walks_own_declines_are_held(self):
        body = definition(self.source, 'DeclineHeld')
        gate = re.search(r'if\((x->decline!=MOVE_DECLINE_\w+(?: &&\s*)?)+\)return 0;', body).group(0)
        # OFFMESH: the lifts the graph cannot yet model.
        self.assertEqual(sorted(re.findall(r'MOVE_DECLINE_\w+', gate)),
                         ['MOVE_DECLINE_CORRIDOR', 'MOVE_DECLINE_MISS', 'MOVE_DECLINE_NO_PATH',
                          'MOVE_DECLINE_OFFMESH'])
        # The hold is the walk's brake and is not a search.
        self.assertIn('Bot2_MoveControl(', body)
        for search in ('StraightSearch(', 'GapSearch(', 'Bot2_ForecastRun(', 'Graph_PlanState('):
            self.assertNotIn(search, body)


class RouteLine(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text(encoding='utf-8')

    def test_route_lines_are_printed_outside_the_searches(self):
        callers = sorted({name for name in re.findall(r'^(?:static )?[\w *]+?\b(\w+)\([^\n;]*\)\s*\{', self.source, re.M)
                          if re.search(r'\bRouteLine\(', definition(self.source, name))
                          and name != 'RouteLine'})
        self.assertEqual(callers, ['Replan', 'RoutePendingPrint'])
        for search in ('StraightSearch', 'GapSearch', 'RerouteInstall', 'StraightCommit'):
            self.assertNotIn('RoutePendingPrint(', definition(self.source, search))
        # The boundary's body; Bot2_MoveParityBeforePmove wraps it in the trace cache's scope.
        boundary = definition(self.source, 'MoveParityBeforePmove')
        self.assertGreater(boundary.rindex('RoutePendingPrint(client);'),
                           boundary.rindex('Bot2_MoveCostPreparation(client,costBegan);'))


if __name__ == '__main__':
    unittest.main()
