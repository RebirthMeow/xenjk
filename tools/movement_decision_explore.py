"""The decision explorer: a beam search over decision sequences, per start class.

The plan's work order
(`docs/movement/PLAN.md`, "Next work order, 2026-09-22: the decision explorer").
Within one start class the live fixture is deterministic to one frame, so it is
the exact simulator and nothing has to be forecast: every branch is measured by
running a whole world to the objective.

  depth 0   the class's control world. Its commit lines are the decisions it
            took, and the `[SPIKE]` lines of each of those searches are the
            candidates the judge admitted there.
  depth k+1 to branch at decision k of a node: one world with that node's own
            first k decisions staged as a decision tape, and
            `bot_moveSelectNth` = k+1 with `bot_moveSelectCand` naming one
            admitted candidate of that search. Every world then runs to the
            objective with the ranking choosing after the branch, so every
            world yields a completed time and a whole decision sequence.
  confirm   before a branch's time can become the incumbent, one more world
            runs its **whole** sequence as a tape, with no prefix and no
            forced index. That is the experiment a tape is asked for later,
            and a branch world is not it: the four-class library campaign
            measured two branch worlds at 16,725 and 15,975 ms whose tapes
            returned 16,875 and 18,675 ms when finally run alone. The
            confirmation's time and census are what the beam and the stopping
            rule read, and a confirmation that does not hit every entry
            excludes its branch, recorded under `unconfirmed`.
  beam      the best B children by confirmed time carry to the next depth.
  stop      when every surviving prefix covers its leg, or when no branch has
            improved on the incumbent for `--patience` depths.

Route branching (`--route-branch`, off by default). Every candidate a search
offers comes from the one route the planner installed, so the forced-index
beam can never leave that route
(`docs/movement/records/2026-09-22-seed-deaths-and-planner-reach.md`, B).
Before the beam, one more world per route alternative, each run with a planner
directive for the leg's first plan (`bot_movePlanGen=1`, the release route):

  avoid     for each edge on the control's release route (its `[ROUTE]` line),
            `bot_movePlanAvoid=<edge>`: the route the planner takes without it.
  require   for each state edge of a `--reference-tape`, resolved to the
            explored graph's index through the fold identity of
            `movement_graph_merge` (the tape's own world's saved cache, or
            `--reference-graph`), that the control's route lacks:
            `bot_movePlanRequire=<edge>`.

A route world is a root only if its release plan line says the directive
applied and its release route shows it (the avoided edge absent, the required
edge present with `require_met=1`), and its route is not one an earlier root
already took; anything else is retained and recorded as a route miss or a
duplicate. Each root has its own confirmation replay - its whole leg as a tape,
under the same directive - and competes with the control for the depth-0 beam
by confirmed time. Every world grown from a route root carries its directive,
confirmations included, so the result of a route root's line is a tape **plus
a directive**; the tape library cannot carry a directive yet.

Nothing about admission, physics, cadence or the ranking weights is touched:
`bot_moveSelectNth` can only select a candidate the judge already admitted, and
a prefix entry the world cannot take is reported by the engine's own census
rather than forced. A world whose prefix stopped hitting is retained and kept
out of the beam, because its decisions past that point are not the ones asked
for.

Commit lines and tapes are read and written by `movement_decision_tape`, so
there is one parser for what a decision is. What is added here is the candidate
set of a search, which only the `[SPIKE]` lines carry.

Worlds run one at a time, never in parallel batches, and the campaign is meant
to be launched detached with its progress file
(`docs/movement/LESSONS.md`). It resumes from that file: a world already in it,
or already retained with a `result.json`, is never run again.

Usage:
    python tools/movement_decision_explore.py --build <dir> --out <dir>
        --corner-class 75 [--graph <seed>] [--beam 3] [--max-depth N]
        [--max-branches N] [--patience 2] [--port 37400]
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import movement_decision_tape as tape
from movement_graph_enrich import read_routes, read_sections  # noqa: E402
from movement_graph_merge import edge_key  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# The judge's own admitted set, as `StraightAdmitted` in bot2_move.c names it.
# Only these can be forced, so only these are branches.
ADMITTED = ('admitted', 'arrives', 'rerouted', 'tap_onward')

# The decision-tape record's settings block, "The settings every world runs".
# The explorer does not choose these; it inherits them so its worlds are
# comparable with the transfer collection's.
BASE = ['--filter', '^red_out_L1_stand$', '--edges', '--parity', '256',
        '--parallel', '1', '--momentum-trace',
        '--set', 'bot_moveReplay=0',
        '--set', 'bot_moveStatePrefer=1', '--set', 'bot_moveReroute=1',
        '--set', 'bot_moveExitSpeed=500', '--set', 'bot_forecastItems=1',
        '--set', 'bot_forecastTriggerShadow=0']


def base_settings(overrides):
    """BASE with each `--set CVAR=VALUE` override replacing BASE's own value
    for that cvar, or appended when BASE does not set it. One value per cvar,
    so a world's command never carries two contradictory settings."""
    # The last override of a cvar wins, as it would on a command line.
    last = {}
    for setting in overrides or []:
        last.pop(setting.split('=', 1)[0], None)
        last[setting.split('=', 1)[0]] = setting
    out, pending = list(BASE), list(last.values())
    for setting in list(pending):
        name = setting.split('=', 1)[0]
        for i in range(len(out) - 1):
            if out[i] == '--set' and out[i + 1].split('=', 1)[0] == name:
                out[i + 1] = setting
                pending.remove(setting)
                break
    for setting in pending:
        out += ['--set', setting]
    return out

SPIKE = re.compile(r'\[SPIKE\] client=(?P<client>\d+) level=(?P<level>-?\d+) (?P<rest>.*)$')
CENSUS = re.compile(r'\[TAPE\] census entry=(\d+) (.*)$')
PLAN_PATH = re.compile(r'\[MOVE\] client=(?P<client>\d+) level=(?P<level>-?\d+) event=plan edge=none '
                       r'reason=path (?P<rest>.*)$')
TOTALS = re.compile(r'\[TAPE\] census entries=(\d+) (.*)$')
FIELD = tape.FIELD


class ExploreError(Exception):
    """The search cannot proceed on what the evidence says. Never guessed past."""


# ---------------------------------------------------------------------------
# Reading a world


def case_directory(world):
    """The directory of the world that actually ran the case.

    A pinned start retires the worlds that missed it beside the one that hit,
    under `<case>.miss-N`, and those sort before `<case>` - so the case name
    from the bench's own summary is what names the trip, never the first log
    found.
    """
    world = Path(world)
    summary = world / 'result.json'
    if summary.is_file():
        try:
            cases = json.loads(summary.read_text(encoding='utf-8')).get('cases') or []
        except ValueError:
            cases = []
        for case in cases:
            if case.get('name') and (world / case['name']).is_dir():
                return world / case['name']
    return world


def world_text(world):
    return tape.server_log(case_directory(world)).read_text(errors='replace')


def candidates(text, client, level):
    """Every candidate the search at `level` judged, in the engine's own order.

    `cand=` is the index into the search's own results array, which is exactly
    what `bot_moveSelectCand` names, so the index is carried through unchanged.
    """
    found = []
    for line in text.splitlines():
        head = SPIKE.search(line)
        if not head or int(head.group('client')) != client or int(head.group('level')) != level:
            continue
        fields = dict(FIELD.findall(head.group('rest')))
        if 'cand' not in fields or 'outcome' not in fields:
            continue
        found.append(dict(cand=int(fields['cand']), kind=fields.get('kind'),
                          plan=int(fields.get('plan', -1)), hops=int(fields.get('hops', 0)),
                          d0=fields.get('d0'), r0=fields.get('r0'),
                          d1=fields.get('d1'), r1=fields.get('r1'),
                          cell=int(fields.get('a0_cell', -1)),
                          cell1=int(fields.get('a1_cell', -1)),
                          progress=float(fields.get('progress', 0)),
                          outcome=fields['outcome'],
                          admitted=fields['outcome'] in ADMITTED))
    found.sort(key=lambda row: row['cand'])
    return found


def admitted_candidates(text, entry):
    """The branches available at one decision of a world.

    A decision is a tape entry, which carries the level and the client the
    commit was printed with, so the candidate set is the one that search judged
    and nothing else. The candidate the world itself committed is marked: it is
    a branch like any other and re-running it measures reproducibility, but a
    caller may want to know which one it is.
    """
    rows = [row for row in candidates(text, entry['client'], entry['level']) if row['admitted']]
    for row in rows:
        row['committed'] = (row['kind'] == entry['kind'] and row['hops'] == entry['hops'] and
                            row['d0'] == entry['aim'] and row['r0'] == entry['runup'] and
                            row['d1'] == entry['aim1'] and row['r1'] == entry['runup1'])
    return rows


def unnameable(text, release, end):
    """The first commit of the leg no tape can name, or None.

    A straight plan is named by the cell each hop lands in, and an arriving hop
    never lands: `arrives=1` prints `c0=-1`, and `movement_decision_tape`
    refuses the world rather than name it. That is the right refusal for a
    whole tape and it would stop the explorer reading any leg that ends on an
    arrival, which at class 75 is most of them. So the leg is read up to that
    commit and the explorer branches no further: the decisions before it are
    exactly the ones a prefix can carry.
    """
    for line in text.splitlines():
        head = tape.SEQUENCE.search(line)
        if not head:
            continue
        level = int(head.group('level'))
        if level <= release or (end is not None and level > end):
            continue
        fields = dict(tape.FIELD.findall(line.split('reason=sequence', 1)[1]))
        if head.group('kind') not in tape.KINDS:
            return level   # a climb, which tapes do not carry
        if head.group('kind') != 'state' and int(fields.get('c0', -1)) < 0:
            return level
    return None


def leg(world):
    """A world's leg decisions, as far as a tape can name them.

    Everything here is `movement_decision_tape`'s: the binding, the release,
    the leg end and the entries. What is added is the stop at the first
    unnameable commit, recorded rather than worked around.
    """
    log = tape.server_log(case_directory(world))
    text = log.read_text(errors='replace')
    name, checksum, graph = tape.binding(text)
    release, release_source = tape.corner_level(text, log)
    end, end_source = tape.leg_end(text, log, release)
    stop = unnameable(text, release, end)
    entries = tape.entries(text, release, stop - 1 if stop is not None else end)
    return dict(map=name, map_checksum=checksum, graph=graph, corner_level=release,
                corner_level_source=release_source, leg_end_level=end,
                leg_end_source=end_source, source=str(log), entries=entries,
                unnameable_level=stop, text=text)


def prefix_text(asset, k):
    """The first `k` decisions of a world, as a tape the bench can stage.

    Built through `movement_decision_tape.tape_text`, so a prefix is the same
    object as a whole tape and the engine binds it to the same map and
    checksum. `k` of 0 is not a tape at all: the world runs with the tape unset
    and only the forced index.
    """
    if k < 0 or k > len(asset['entries']):
        raise ExploreError('a prefix of %d decisions is not available in a leg of %d'
                           % (k, len(asset['entries'])))
    return tape.tape_text(asset['map'], asset['map_checksum'], asset['corner_level'],
                          asset['entries'][:k], asset['source'], asset.get('graph', 'none'))


def census(text):
    """The engine's end-of-leg report on the prefix it was given."""
    entries, totals = [], None
    for line in text.splitlines():
        found = CENSUS.search(line)
        if found:
            record = dict(entry=int(found.group(1)))
            for token in found.group(2).split():
                key, _, value = token.partition('=')
                record[key] = value
            entries.append(record)
            continue
        found = TOTALS.search(line)
        if found:
            totals = dict(entries=int(found.group(1)))
            for token in found.group(2).split():
                key, _, value = token.partition('=')
                # The totals line carries the tape library's selection as well
                # as the tallies - the leg-id, the rung and the file chosen -
                # so a field is kept as it was printed when it is not a count.
                try:
                    totals[key] = int(value)
                except ValueError:
                    totals[key] = value
    return dict(entries=entries, totals=totals)


def forced_commit(text, nth, release=None):
    """Whether the forced index actually fired, from the commit's own line.

    `select_forced=1` is the engine saying it took the named candidate;
    `select_forced=-1` is it saying the name was not an admitted candidate of
    that set, which is a branch that did not happen and not a result.

    Only commits of the leg count. The commit counter restarts at the corner
    release, so the bot's own pre-release walk carries the same ordinals and
    the override is offered there too - and refuses there, on a candidate set
    from wherever the bot had wandered to. Reading that line instead of the
    leg's would report every branch as a branch that did not happen. The
    release teleports the runner, so what the override did before it changes
    nothing about the trip.
    """
    for line in text.splitlines():
        head = tape.SEQUENCE.search(line)
        if not head:
            continue
        if release is not None and int(head.group('level')) <= release:
            continue
        fields = dict(FIELD.findall(line.split('reason=sequence', 1)[1]))
        if fields.get('select_forced') in ('1', '-1') and int(fields.get('commit_nth', -1)) == nth:
            return int(fields['select_forced'])
    return 0


def summarize(world, name, depth, parent, branch, seconds=None, returncode=None, extra=None):
    """One world's record: what was asked of it, and what it did.

    `decisions` is its own committed leg, which is what a child branches from.
    A world that did not complete carries its failure and no decisions.
    """
    record = dict(world=name, depth=depth, parent=parent, branch=branch,
                  out=str(world), seconds=seconds, returncode=returncode)
    record.update(extra or {})
    result = world / 'result.json'
    case = {}
    if result.is_file():
        try:
            case = (json.loads(result.read_text(encoding='utf-8')).get('cases') or [{}])[0]
        except ValueError:
            case = {}
    record.update(status=case.get('status'), reason=case.get('reason'),
                  time_ms=case.get('time_ms'), corner_level=case.get('corner_level'),
                  corner_class=case.get('corner_level') % 200
                  if isinstance(case.get('corner_level'), int) else None,
                  deaths=case.get('deaths'), owners=case.get('owners'),
                  parity_mismatches=(case.get('parity') or {}).get('mismatches'),
                  straight_commits=(case.get('straight') or {}).get('commits'),
                  state_commits=(case.get('state') or {}).get('commits'))
    try:
        text = world_text(world)
    except tape.TapeError:
        record.update(error='no server.log under the world', decisions=None)
        return record
    record['census'] = census(text)
    record['forced'] = forced_commit(text, branch['nth'],
                                     record.get('corner_level')) if branch else None
    try:
        asset = leg(world)
        record['decisions'] = asset['entries']
        record['leg'] = len(asset['entries'])
        record['unnameable_level'] = asset['unnameable_level']
    except tape.TapeError as error:
        record.update(decisions=None, leg=None, error=str(error))
    record['prefix_broken'] = prefix_broken(record)
    release = record.get('corner_level')
    if isinstance(release, int):
        route = release_route(text, release)
        record['release_route'] = route
        if record.get('directive'):
            record['route_taken'] = route_taken(route, record['directive'])
    return record


# ---------------------------------------------------------------------------
# Routes


def release_route(text, release):
    """The leg's first plan and the route it installed, or None.

    The release is a `Move_Reset`, so the leg's first plan is the first
    `reason=path` plan line at or after the release level with `replans=1`;
    the route is the `[ROUTE]` line of the same path generation. Its directive
    fields are present only when a directive applied to it.
    """
    plan = None
    for line in text.splitlines():
        found = PLAN_PATH.search(line)
        if not found or int(found.group('level')) < release:
            continue
        fields = dict(FIELD.findall(found.group('rest')))
        if fields.get('replans') == '1':
            plan = dict(client=int(found.group('client')), level=int(found.group('level')),
                        fields=fields)
            break
    if not plan:
        return None
    generation = int(plan['fields'].get('gen', -1))
    routes, _ = read_routes(line for line in text.splitlines() if '[ROUTE]' in line)
    route = next((r for r in routes if r['generation'] == generation
                  and r['client'] == plan['client']), None)
    fields = plan['fields']
    return dict(level=plan['level'], generation=generation,
                cost_ms=int(fields.get('cost_ms', -1)), cells=int(fields.get('cells', -1)),
                edges=route['edges'] if route else None,
                directive=int(fields.get('directive', 0)),
                plan_avoid=int(fields.get('plan_avoid', 0)),
                require=int(fields.get('require', -1)),
                require_met=int(fields.get('require_met', 0)))


def route_taken(route, directive):
    """Whether the release route is the one the directive asked for."""
    if not route or route.get('edges') is None or not route.get('directive'):
        return False
    edges = route['edges']
    for edge in directive.get('avoid') or []:
        if edge in edges:
            return False
    if directive.get('avoid') and not route.get('plan_avoid'):
        return False
    require = directive.get('require')
    if require is not None:
        return bool(route.get('require_met') and route.get('require') == require and require in edges)
    return True


def tape_rows(path):
    """A tape file's entries: kind and both edges, as the engine reads them."""
    rows = []
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        parts = line.split()
        if not parts or parts[0] != 'e' or len(parts) < 15:
            continue
        rows.append(dict(index=int(parts[1]), kind=parts[5], edge=int(parts[11]), edge2=int(parts[12])))
    return rows


def tape_source_cache(path):
    """The saved cache of the world a tape was recorded in, from its `# source=` line."""
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if line.startswith('# source='):
            log = Path(line.partition('=')[2].strip())
            caches = sorted((log.parent / 'base' / 'bot2-graph').glob('*.txt'))
            if len(caches) == 1:
                return caches[0]
            raise ExploreError('the reference tape\'s world %s has %d saved caches, not one'
                               % (log.parent, len(caches)))
    raise ExploreError('the reference tape names no source world; pass --reference-graph')


def reference_edges(tape_path, reference_graph, explored_graph):
    """The reference tape's state edges as indices of the explored graph.

    An index is local to its graph, so each is read from the graph the tape
    was recorded on and matched in the explored graph by the fold identity
    (type, cells, tap, entry band). Returns (resolved, unresolved): resolved
    is a list of dicts with the reference and explored index, in tape order
    and without repeats; unresolved names the reference indices with no match.
    """
    reference = reference_graph or tape_source_cache(tape_path)
    ref_edges = read_sections(reference)[4]
    explored = {}
    for index, line in enumerate(read_sections(explored_graph)[4]):
        explored.setdefault(edge_key(line.split()), index)
    resolved, unresolved, seen = [], [], set()
    for row in tape_rows(tape_path):
        if row['kind'] != 'state':
            continue
        for index in (row['edge'], row['edge2']):
            if index < 0 or index in seen:
                continue
            seen.add(index)
            if index >= len(ref_edges):
                unresolved.append(dict(reference=index, why='beyond_reference_graph'))
                continue
            found = explored.get(edge_key(ref_edges[index].split()))
            if found is None:
                unresolved.append(dict(reference=index, why='absent_from_explored_graph'))
            else:
                resolved.append(dict(reference=index, edge=found, entry=row['index']))
    return resolved, unresolved


def prefix_broken(record):
    """True when the prefix this world was given stopped being taken.

    The branch is only the decision it was asked for if every earlier decision
    was the prefix's. The engine's census says so directly: one hit per entry,
    no refused and no absent.
    """
    branch = record.get('branch')
    if not branch or not branch.get('prefix'):
        return False
    totals = (record.get('census') or {}).get('totals')
    if not totals:
        return True
    return (totals.get('hits') != branch['prefix'] or totals.get('refused')
            or totals.get('absent'))


def completed(record):
    """The world ran the case to a verdict.

    A world that never landed on the pinned class, or died on the harness, has
    measured nothing: the bench gives a pinned class four fresh worlds and a
    world's play ladder offers only four of the eight classes, so a call misses
    outright about one time in eight. That is retried under a new name, never
    in place.
    """
    return bool(record.get('status') == 'pass' and record.get('time_ms'))


def usable(record):
    """A world that can carry the beam: it completed, and it did what was asked."""
    return bool(completed(record) and record.get('decisions')
                and not record.get('prefix_broken')
                and record.get('forced') in (None, 1)
                and (not record.get('directive') or record.get('route_taken'))
                and not record.get('route_duplicate'))


# ---------------------------------------------------------------------------
# The search


def confirmed_hit_all(record):
    """A confirmation world that took every entry of the tape it was given.

    The census totals are the engine's own report: one hit per entry, nothing
    refused and nothing absent. Anything less and the world did not execute the
    sequence, so its time is not that sequence's time.
    """
    totals = (record.get('census') or {}).get('totals')
    if not totals:
        return False
    return bool(totals.get('entries') and totals.get('hits') == totals.get('entries')
                and not totals.get('refused') and not totals.get('absent'))


def confirmed(record):
    """A branch whose confirmation world completed and hit every entry.

    The control world confirms nothing: it ran under no tape and no forced
    index, so its own time is already a fresh world's time. A confirmation
    world is never itself a node: it is a measurement of its branch, and
    letting it compete would put a rejected sequence's world back in the beam
    under another name.
    """
    if record.get('confirm_of'):
        return False
    if not record.get('branch') and not record.get('directive'):
        return usable(record)
    check = record.get('confirmation')
    return bool(usable(record) and check and completed(check) and confirmed_hit_all(check))


def score_time(record):
    """The time a record carries into the beam and the stopping rule.

    For a branch that is the **confirmation** world's time, never the branch
    world's: a branch ran under a prefix tape plus a forced index, and the
    four-class library campaign measured what that costs - two explorer tapes
    whose branch worlds completed in 16,725 and 15,975 ms returned 16,875 and
    18,675 ms when the whole sequence was finally run alone
    (`records/2026-09-22-four-class-library.md`).
    """
    if (record.get('branch') or record.get('directive')) and record.get('confirmation'):
        return record['confirmation'].get('time_ms')
    return record.get('time_ms')


def beam(records, width):
    """The best `width` confirmed worlds by confirmed time, fastest first."""
    return sorted([r for r in records if confirmed(r)], key=score_time)[:width]


def stalled(history, patience):
    """True when no depth in the last `patience` improved on the incumbent.

    `history` is one boolean per completed depth: did any branch at that depth
    beat the best time seen before it.
    """
    return len(history) >= patience and not any(history[-patience:])


def branch_name(parent, depth, cand):
    return '%s-d%dc%d' % (parent, depth + 1, cand)


def load_progress(path):
    """Every world this campaign has already run, by name."""
    done = {}
    if Path(path).is_file():
        for line in Path(path).read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if record.get('world'):
                done[record['world']] = record
    return done


def append_progress(path, record):
    with Path(path).open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(record) + '\n')


class Campaign(object):
    """The beam search over one start class.

    `execute` runs one world and returns its directory; it is a parameter so
    the search can be tested on hand-built worlds without a game.
    """

    def __init__(self, options, execute):
        self.options = options
        self.execute = execute
        self.out = Path(options.out)
        self.progress = self.out / 'progress.jsonl'
        self.done = load_progress(self.progress)
        self.records = []
        # Branches that were fast enough to be confirmed and were not: the
        # campaign's own record of a sequence that does not reproduce.
        self.unconfirmed = []
        self.route_worlds = []
        # The reference route's state edges, resolved to the explored graph.
        self.reference, self.reference_unresolved = [], []
        if getattr(options, 'reference_tape', None):
            self.reference, self.reference_unresolved = reference_edges(
                options.reference_tape, getattr(options, 'reference_graph', None), options.graph)

    def log(self, text):
        print(text, flush=True)

    def world(self, name, depth, parent, branch, tape_text=None, extra=None, directive=None):
        """One world, run or resumed, retried under a new name if it never ran.

        Every attempt is retained and appears in the progress file; the first
        that reaches a verdict is the one the search reads. A planner directive
        is carried on the record, so every world grown from it inherits it.
        """
        record = None
        if directive:
            extra = dict(extra or {}, directive=directive)
        for attempt in range(1, max(1, self.options.world_attempts) + 1):
            record = self.attempt(name if attempt == 1 else '%s-a%d' % (name, attempt),
                                  depth, parent, branch, tape_text, extra)
            if completed(record):
                return record
        return record

    def attempt(self, name, depth, parent, branch, tape_text=None, extra=None):
        """One world, run or resumed. Never run twice."""
        if name in self.done:
            record = self.done[name]
            self.records.append(record)
            self.log('resumed %s: %s %s' % (name, record.get('status'), record.get('time_ms')))
            return record
        directory = self.out / name
        started = time.time()
        returncode = None
        if not (directory / 'result.json').is_file():
            path = None
            if tape_text is not None:
                tapes = self.out / 'tapes'
                tapes.mkdir(parents=True, exist_ok=True)
                path = tapes / ('%s.tape' % name)
                path.write_text(tape_text, encoding='utf-8')
            directive = (extra or {}).get('directive')
            if directive:
                returncode = self.execute(name, directory, path, branch, directive=directive)
            else:
                returncode = self.execute(name, directory, path, branch)
        record = summarize(directory, name, depth, parent, branch,
                           round(time.time() - started, 1), returncode, extra)
        append_progress(self.progress, record)
        self.done[name] = record
        self.records.append(record)
        self.log('ran %s: %s time_ms=%s forced=%s prefix_broken=%s'
                 % (name, record.get('status'), record.get('time_ms'),
                    record.get('forced'), record.get('prefix_broken')))
        return record

    def confirm(self, record):
        """One fresh world running only this branch's whole sequence as a tape.

        No prefix and no forced index: exactly the experiment a tape is asked
        for later, which a branch world is not. The confirmation's own time and
        census are what the beam and the stopping rule read, and a branch whose
        confirmation does not hit every entry is recorded on the branch and
        excluded from the beam rather than dropped silently.

        Resumable like any other world, and never run twice: the confirmation
        appears in the progress file under `<branch>-cf` and is attached to its
        branch by name on resume.
        """
        if record.get('confirmation') is not None:
            return record['confirmation']
        name = '%s-cf' % record['world']
        try:
            asset = leg(Path(record['out']))
        except tape.TapeError as error:
            record['confirm_error'] = str(error)
            record['confirmation'] = {}
            return record['confirmation']
        if not asset['entries']:
            record['confirm_error'] = 'the branch leg names no decision'
            record['confirmation'] = {}
            return record['confirmation']
        # Through `world` and not out of `done` directly, so a resumed
        # confirmation is resumed exactly as every other world is and appears
        # once in the campaign's records either way.
        check = self.world(name, record['depth'], record['world'], None,
                           tape_file(asset),
                           dict(confirm_of=record['world'],
                                confirm_entries=len(asset['entries'])),
                           directive=record.get('directive'))
        record['confirmation'] = check
        self.log('confirmed %s: %s time_ms=%s branch_time_ms=%s hit_all=%s'
                 % (record['world'], check.get('status'), check.get('time_ms'),
                    record.get('time_ms'), confirmed_hit_all(check)))
        return check

    def select(self, grown, width):
        """The depth's beam, confirmed in branch-time order until it is full.

        Only a world that could become the incumbent is confirmed, so the
        confirmation costs one world per beam slot and one per rejection, not
        one per branch.
        """
        kept, rejected = [], []
        for record in sorted([r for r in grown if usable(r)], key=lambda r: r['time_ms']):
            if len(kept) >= width:
                break
            self.confirm(record)
            (kept if confirmed(record) else rejected).append(record)
        self.unconfirmed.extend(
            dict(world=r['world'], branch_time_ms=r['time_ms'],
                 confirmation=(r.get('confirmation') or {}).get('world'),
                 confirm_time_ms=(r.get('confirmation') or {}).get('time_ms'),
                 census=((r.get('confirmation') or {}).get('census') or {}).get('totals'),
                 error=r.get('confirm_error'))
            for r in rejected)
        return sorted(kept, key=score_time)[:width]

    def children(self, node):
        """Every admitted candidate at the node's next decision, run once each."""
        depth = node['depth']
        built = leg(Path(node['out']))
        text = built['text']
        if depth >= len(built['entries']):
            self.log('%s: the prefix covers the leg (%d decisions%s)'
                     % (node['world'], len(built['entries']),
                        ', the last of them an arrival no tape can name'
                        if built['unnameable_level'] else ''))
            return []
        entry = built['entries'][depth]
        offers = admitted_candidates(text, entry)
        if self.options.max_branches:
            offers = offers[:self.options.max_branches]
        out = []
        for offer in offers:
            name = branch_name(node['world'], depth, offer['cand'])
            branch = dict(nth=entry['commit_nth'], cand=offer['cand'], level=entry['level'],
                          prefix=depth, kind=offer['kind'], hops=offer['hops'],
                          committed=offer.get('committed'), parent=node['world'],
                          # The leg's commits are numbered from the release, so
                          # the k-th decision is the k+1-th commit. When a world
                          # prints otherwise the force would land elsewhere, and
                          # the record says so rather than the campaign assuming.
                          nth_is_depth=entry['commit_nth'] == depth + 1)
            out.append(self.world(name, depth + 1, node['world'], branch,
                                  prefix_text(built, depth) if depth else None,
                                  directive=node.get('directive')))
        return out

    def route_roots(self, root):
        """The route alternatives of the leg, one world each, before the beam.

        Returns every route world run (the report's), with `route_duplicate`
        set on a world whose release route an earlier root already took. Only
        a world whose directive took and whose route is new is confirmed.
        """
        route = root.get('release_route') or {}
        edges = route.get('edges')
        if not edges:
            raise ExploreError('the control world printed no release route; nothing to branch over')
        # The named inclusions first: they are the few alternatives a reference
        # asks for, so a capped run keeps them; then one deviation per edge.
        offers = []
        for ref in self.reference:
            if ref['edge'] not in edges and ('rq', ref['edge']) not in [(o[0], o[1]) for o in offers]:
                offers.append(('rq', ref['edge'], dict(require=ref['edge'], gen=1)))
        for edge in edges:
            if edge >= 0 and ('av', edge) not in [(o[0], o[1]) for o in offers]:
                offers.append(('av', edge, dict(avoid=[edge], gen=1)))
        if getattr(self.options, 'route_branches', 0):
            offers = offers[:self.options.route_branches]
        seen = {tuple(edges): root['world']}
        worlds = []
        for tag, edge, directive in offers:
            name = '%s-%s%d' % (root['world'], tag, edge)
            record = self.world(name, 0, root['world'], None,
                                extra=dict(route_branch=dict(kind=tag, edge=edge)),
                                directive=directive)
            taken = (record.get('release_route') or {}).get('edges')
            if record.get('route_taken') and taken is not None:
                key = tuple(taken)
                if key in seen:
                    record['route_duplicate'] = seen[key]
                else:
                    seen[key] = record['world']
            worlds.append(record)
            self.log('route %s: %s time_ms=%s taken=%s duplicate_of=%s'
                     % (name, record.get('status'), record.get('time_ms'),
                        record.get('route_taken'), record.get('route_duplicate')))
        for record in worlds:
            if usable(record):
                self.confirm(record)
        return worlds

    def run(self):
        root = self.world(self.options.root_name, 0, None, None)
        if not usable(root):
            raise ExploreError('the control world at class %s did not complete; nothing branches'
                               % self.options.corner_class)
        frontier = [root]
        incumbent = root['time_ms']
        self.route_worlds = []
        if getattr(self.options, 'route_branch', False):
            self.route_worlds = self.route_roots(root)
            frontier = beam([root] + self.route_worlds, self.options.beam)
            incumbent = score_time(frontier[0])
            self.log('roots: %d route worlds, %d confirmed; depth-0 beam %s, incumbent %d ms'
                     % (len(self.route_worlds), len([r for r in self.route_worlds if confirmed(r)]),
                        [r['world'] for r in frontier], incumbent))
        history = []
        depth = 0
        while True:
            if self.options.max_depth and depth >= self.options.max_depth:
                stop = 'max depth %d' % self.options.max_depth
                break
            grown = []
            for node in frontier:
                grown.extend(self.children(node))
            if not grown:
                stop = 'every surviving prefix covers its leg'
                break
            best = self.select(grown, self.options.beam)
            improved = bool(best) and score_time(best[0]) < incumbent
            if improved:
                incumbent = score_time(best[0])
            history.append(improved)
            if not best:
                stop = 'no branch at depth %d completed and confirmed as asked' % (depth + 1)
                break
            frontier = best
            depth += 1
            self.log('depth %d: %d branches, best confirmed %d ms, incumbent %d ms'
                     % (depth, len(grown), score_time(best[0]), incumbent))
            if stalled(history, self.options.patience):
                stop = 'no branch improved the incumbent for %d depths' % self.options.patience
                break
        return self.report(stop, depth)

    def report(self, stop, depth):
        # Only a confirmed record can be the campaign's result: its time is a
        # fresh tape-only world's, which is the only time a tape can be asked
        # to reproduce.
        usable_records = [r for r in self.records if confirmed(r)]
        best = min(usable_records, key=score_time) if usable_records else None
        summary = dict(corner_class=self.options.corner_class, build=str(self.options.build),
                       beam=self.options.beam, patience=self.options.patience,
                       max_branches=self.options.max_branches, max_depth=self.options.max_depth,
                       depths=depth, stop=stop, worlds=len(self.records),
                       completed=len(usable_records),
                       confirmed_time_ms=score_time(best) if best else None,
                       confirmation=(best or {}).get('confirmation', {}).get('world'),
                       unconfirmed=self.unconfirmed,
                       control=next((r for r in self.records
                                     if r['world'] == self.options.root_name), None),
                       route_branch=bool(getattr(self.options, 'route_branch', False)),
                       reference=self.reference, reference_unresolved=self.reference_unresolved,
                       route_worlds=[dict(world=r['world'], route_branch=r.get('route_branch'),
                                          directive=r.get('directive'), status=r.get('status'),
                                          time_ms=r.get('time_ms'), route_taken=r.get('route_taken'),
                                          route_duplicate=r.get('route_duplicate'),
                                          release_route=r.get('release_route'),
                                          confirmed=confirmed(r),
                                          confirm_time_ms=(r.get('confirmation') or {}).get('time_ms'))
                                     for r in self.route_worlds],
                       directive=(best or {}).get('directive'),
                       best=best, tape=None,
                       sequence=[dict(index=e['index'], level=e['level'], kind=e['kind'],
                                      hops=e['hops'], edge=e['edge'], edge2=e['edge2'],
                                      cell=e['cell'], cell1=e['cell1'],
                                      commit_nth=e['commit_nth'])
                                 for e in (best or {}).get('decisions') or []])
        if best:
            tapes = self.out / 'tapes'
            tapes.mkdir(parents=True, exist_ok=True)
            path = tapes / ('c%s-best.tape' % self.options.corner_class)
            asset = leg(Path(best['out']))
            path.write_text(tape_file(asset), encoding='utf-8')
            asset.pop('text')
            path.with_suffix(path.suffix + '.json').write_text(json.dumps(asset, indent=2),
                                                               encoding='utf-8')
            summary['tape'] = str(path)
            summary['tape_unnameable_level'] = asset['unnameable_level']
        (self.out / ('result-c%s.json' % self.options.corner_class)).write_text(
            json.dumps(summary, indent=2), encoding='utf-8')
        self.log(json.dumps({k: v for k, v in summary.items()
                             if k not in ('control', 'best', 'sequence', 'unconfirmed', 'route_worlds')}))
        return summary


def tape_file(asset):
    return tape.tape_text(asset['map'], asset['map_checksum'], asset['corner_level'],
                          asset['entries'], asset['source'], asset.get('graph', 'none'))


# ---------------------------------------------------------------------------
# Driving the bench


def directive_settings(directive):
    """The bench `--set` pairs for a planner directive, or none."""
    if not directive:
        return []
    out = []
    if directive.get('avoid'):
        out += ['--set', 'bot_movePlanAvoid=%s' % ','.join(str(e) for e in directive['avoid'])]
    if directive.get('require') is not None:
        out += ['--set', 'bot_movePlanRequire=%d' % directive['require']]
    out += ['--set', 'bot_movePlanGen=%d' % directive.get('gen', 0)]
    return out


def bench_command(options, directory, tape_path, branch, port, directive=None):
    command = [sys.executable, str(ROOT / 'tools/movement_bench.py'),
               '--build', str(options.build), '--source-root', str(options.source_root),
               '--out', str(directory), '--port', str(port),
               '--corner-class', str(options.corner_class),
               '--corner-class-min', str(options.corner_class_min),
               '--corner-class-max', str(options.corner_class_max),
               '--corner-level-attempts', str(options.attempts)] + base_settings(getattr(options, 'set', None))
    # The case BASE names is the level-1 stand; --case explores another.
    command[command.index('--filter') + 1] = '^%s$' % getattr(options, 'case', 'red_out_L1_stand')
    if options.graph:
        command += ['--graph', str(options.graph)]
    if tape_path:
        command += ['--decision-tape', str(tape_path)]
    if branch:
        command += ['--set', 'bot_moveSelectNth=%d' % branch['nth'],
                    '--set', 'bot_moveSelectCand=%d' % branch['cand']]
    return command + directive_settings(directive)


def bench_runner(options):
    """One world per call, sequential. Ports never collide inside a campaign."""
    state = dict(port=options.port)

    def execute(name, directory, tape_path, branch, directive=None):
        command = bench_command(options, directory, tape_path, branch, state['port'], directive)
        state['port'] += 20
        run = subprocess.run(command, cwd=str(ROOT), capture_output=True, text=True)
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'runner.stdout.txt').write_text(run.stdout or '', encoding='utf-8')
        (directory / 'runner.stderr.txt').write_text(run.stderr or '', encoding='utf-8')
        (directory / 'command.json').write_text(json.dumps(command, indent=2), encoding='utf-8')
        return run.returncode

    return execute


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--graph', type=Path)
    parser.add_argument('--corner-class', type=int, required=True)
    parser.add_argument('--corner-class-min', type=int, default=3000)
    parser.add_argument('--corner-class-max', type=int, default=5500)
    parser.add_argument('--attempts', type=int, default=4,
                        help='fresh worlds the bench may try for the pinned class (default 4)')
    parser.add_argument('--world-attempts', type=int, default=3,
                        help='fresh bench calls for a world that never reached a verdict, '
                             'each retained under its own name (default 3)')
    parser.add_argument('--beam', type=int, default=3, help='sequences kept per depth (default 3)')
    parser.add_argument('--max-depth', type=int, default=0, help='0 runs to the stop rule')
    parser.add_argument('--max-branches', type=int, default=0,
                        help='cap on branches per decision; 0 takes every admitted candidate. '
                             'A budget control for a smoke run, not part of the search')
    parser.add_argument('--patience', type=int, default=2,
                        help='depths without an improvement before stopping (default 2)')
    parser.add_argument('--port', type=int, default=37400)
    parser.add_argument('--root-name', default='control')
    parser.add_argument('--route-branch', action='store_true',
                        help='before the beam, one world per release-route edge avoided and per '
                             'reference state edge required (bot_movePlanAvoid / bot_movePlanRequire '
                             'on the leg\'s first plan); each that takes is a root with its own confirmation')
    parser.add_argument('--reference-tape', type=Path,
                        help='a tape whose state edges the route branching requires, one world each')
    parser.add_argument('--reference-graph', type=Path,
                        help="the graph the reference tape's indices name; default: its own world's saved cache")
    parser.add_argument('--route-branches', type=int, default=0,
                        help='cap on route worlds; 0 runs every one. A budget control for a smoke run')
    parser.add_argument('--case', default='red_out_L1_stand',
                        help='the case every world runs (default the level-1 stand); red_out_L2_stand '
                             'explores the Speed leg, whose library key carries its spend')
    parser.add_argument('--set', action='append', default=[], metavar='CVAR=VALUE',
                        help="a world cvar for every world, replacing BASE's value for the same cvar")
    options = parser.parse_args(argv)
    if options.reference_tape and not options.graph:
        parser.error('--reference-tape needs --graph: the reference edges are resolved to its indices')
    options.out = options.out.resolve()
    options.build = options.build.resolve()
    options.source_root = options.source_root.resolve()
    options.out.mkdir(parents=True, exist_ok=True)
    campaign = Campaign(options, bench_runner(options))
    try:
        campaign.run()
    except ExploreError as error:
        print('decision explorer stopped: %s' % error, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
