"""The search budget's default-off contract, read out of the shipped C source.

`bot_moveSearchBudget` moves the straight search's candidate rollouts onto the
frames before the boundary the search will run at. The whole of its acceptance
is that with the cvar unset nothing changes, so the properties that guarantee
that are worth a regression that fails when the source stops having them:

- the cvar is read through a helper that returns 0 when it is unset;
- the per-frame bound is only consulted when a positive budget is installed,
  so the judge's loops run to their end in every ordinary search;
- the boundary refuses a pre-searched candidate set unless the live player
  state matches the predicted one on the fields the parity harness compares
  and both executors match byte for byte;
- a pre-search never commits.

This reads the source text rather than compiling it. It is a guard on the
contract, not a proof of behaviour: what the budget does to decisions and to
cost is a native measurement, recorded under `docs/movement/records/`.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'codemp/game/bot2_move.c'


def body(text, signature):
    """The text of the function whose definition line contains `signature`.

    A forward declaration ends in a semicolon before its brace, so the
    definition is the occurrence whose next punctuation is the brace.
    """
    start = -1
    at = text.find(signature)
    while at >= 0:
        brace, semi = text.find('{', at), text.find(';', at)
        if brace >= 0 and (semi < 0 or brace < semi):
            start = brace
            break
        at = text.find(signature, at + 1)
    if start < 0:
        raise AssertionError('no definition for ' + signature)
    depth, i = 0, start
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    raise AssertionError('unterminated function for ' + signature)


class SearchBudget(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = SOURCE.read_text(encoding='utf-8', errors='replace')

    def test_the_cvar_defaults_to_four_and_zero_turns_it_off(self):
        reader = body(self.text, 'static int SearchBudget(void)')
        self.assertIn('bot_moveSearchBudget', reader)
        # An unset cvar reads as an empty string: the production default, 4
        # An explicit 0 still turns it off.
        self.assertIn("value[0]?max(0,atoi(value)):MOVE_SEARCH_BUDGET_DEFAULT", reader.replace(' ', ''))
        self.assertIn('#define MOVE_SEARCH_BUDGET_DEFAULT 4', self.text)

    def test_the_per_frame_bound_needs_a_positive_budget(self):
        spent = body(self.text, 'static int SearchBudgetSpent(void)')
        self.assertIn('searchRolloutBudget>0', spent.replace(' ', ''))

    def test_the_step_does_nothing_at_all_with_no_budget(self):
        step = body(self.text, 'static void SearchBudgetStep(')
        head = step[:step.index('if(searched)')]
        self.assertIn('if(!budget)', head.replace(' ', ''))
        self.assertIn('return;', head)

    def test_the_take_compares_the_parity_fields_and_both_executors(self):
        take = body(self.text, 'static int SearchCacheTake(').replace(' ', '')
        self.assertIn('MovementStateDiff(ps,&c->rootPS)', take)
        self.assertIn('memcmp(&m->executor,&c->rootExec', take)
        self.assertIn('memcmp(&m->executorBefore,&c->rootExecBefore', take)
        # And the winner is re-rolled from the actual state before it is taken.
        self.assertIn('SearchCacheValidate(', take)

    def test_the_take_refuses_unless_the_cache_is_for_this_very_frame(self):
        take = body(self.text, 'static int SearchCacheTake(').replace(' ', '')
        self.assertIn('c->targetTime!=level.time', take)
        self.assertIn('c->stage!=PRE_READY', take)

    def test_a_pre_search_never_commits(self):
        search = body(self.text, 'static void StraightSearchCore(')
        commit = search.index('StraightCommit(')
        stop = re.search(r'\n    if\(pre\) \{[^}]*pre->stage=PRE_READY;\s*\n\s*return;\s*\n\s*\}', search)
        self.assertIsNotNone(stop, 'the pre-search must return before the tail')
        self.assertLess(stop.start(), commit,
                        'the pre-search returns before the commit, not after it')

    def test_the_reset_clears_the_prediction(self):
        reset = body(self.text, 'static void SearchBudgetReset(int client)')
        self.assertIn('searchCache[client].stage=PRE_IDLE', reset.replace(' ', ''))
        self.assertIn('SearchBudgetReset(client);', body(self.text, 'void Move_Reset(int client)'))


if __name__ == '__main__':
    unittest.main()
