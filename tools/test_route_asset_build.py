import json
import unittest
import uuid
from pathlib import Path

from route_asset_build import (FORMAT, TAPE_FORMAT, RecordingError, build, command_indices,
                               command_ms, contract, grounded, tape_entries, tape_text)

COLUMNS = ['row', 'level', 'elapsed_ms', 'cmd_time', 'forward', 'right', 'up', 'buttons',
           'pmove_fixed', 'pmove_msec', 'pmove_float',
           'pre_x', 'pre_y', 'pre_z', 'pre_vx', 'pre_vy', 'pre_vz', 'pre_pitch', 'pre_yaw',
           'pre_ground', 'pre_cmd_time', 'pre_health', 'pre_force', 'pre_gravity',
           'pre_jump_level', 'pre_speed_level', 'pre_redflag', 'pre_blueflag',
           'post_x', 'post_y', 'post_z', 'post_vx', 'post_vy', 'post_vz', 'post_pitch',
           'post_yaw', 'post_ground', 'post_cmd_time']

GROUND, AIR = 1022, 1023


def artifacts(name):
    """Test recordings are kept, not deleted: a converter regression that fails
    is only diagnosable from the CSV it was given."""
    path = Path(__file__).resolve().parent.parent / '.codex-research/unit-artifacts' / name
    path.mkdir(parents=True, exist_ok=True)
    return path


def row(index, cmd_time, elapsed, ground, speed=100.0, up=0, yaw=90.0, pre_cmd_time=None,
        post_x='11'):
    """One recorded Pmove call. `pre_cmd_time` is the player state's command
    time going in, so `cmd_time - pre_cmd_time` is the physics the call ran;
    unset, it chains from the command time 8 ms earlier."""
    values = dict.fromkeys(COLUMNS, '0')
    values.update(row=index, level=1000 + elapsed, elapsed_ms=elapsed, cmd_time=cmd_time,
                  forward='127', right='0', up=up, buttons='0',
                  pmove_fixed='1', pmove_msec='8', pmove_float='1',
                  pre_x='10', pre_y='20', pre_z='30', pre_ground=ground, pre_health='100',
                  pre_force='100', pre_gravity='800', pre_jump_level='3', pre_speed_level='3',
                  pre_yaw=yaw, pre_pitch='5',
                  pre_cmd_time=cmd_time - 8 if pre_cmd_time is None else pre_cmd_time,
                  post_x=post_x, post_y='20', post_z='30', post_vx=speed, post_vy='0',
                  post_yaw=yaw, post_pitch='5', post_ground=ground, post_cmd_time=cmd_time)
    return values


def write(rows, name='route-asset-build'):
    target = artifacts(name) / (uuid.uuid4().hex + '.csv')
    with open(target, 'w', encoding='utf-8', newline='') as handle:
        handle.write(','.join(COLUMNS) + '\n')
        for r in rows:
            handle.write(','.join(str(r[c]) for c in COLUMNS) + '\n')
    return target


class GroundTest(unittest.TestCase):
    def test_none_entity_is_airborne(self):
        self.assertIs(grounded(str(AIR)), False)
        self.assertIs(grounded(str(GROUND)), True)
        self.assertIsNone(grounded(''))


class FirstCommandTest(unittest.TestCase):
    """The first command's own physics duration, which the converter used to
    write as zero and playback replaced with one `pmove_msec` step."""

    def test_first_delta_is_the_recorded_duration(self):
        for duration in (8, 16, 32):
            tape = tape_entries([row(0, 100, 0, GROUND, pre_cmd_time=100 - duration)])
            self.assertEqual(tape[0]['dt'], duration, 'first command of %d ms' % duration)

    def test_first_delta_is_not_zero_and_not_a_nominal_step(self):
        """A first command with pre_cmd_time 100, cmd_time 132. The old
        converter wrote 0 and playback ran 8 ms of physics instead of 32."""
        tape = tape_entries([row(0, 132, 0, GROUND, pre_cmd_time=100)])
        self.assertEqual(tape[0]['dt'], 32)
        self.assertNotEqual(tape[0]['dt'], 0)
        self.assertNotEqual(tape[0]['dt'], 8)

    def test_first_jump_keeps_its_four_fixed_steps(self):
        """An 8 ms fixed step divides a 32 ms command into four, and a jump
        given one step is a different jump."""
        tape = tape_entries([row(0, 132, 0, GROUND, up=127, pre_cmd_time=100),
                             row(1, 140, 8, AIR)])
        self.assertEqual(tape[0]['up'], 127)
        self.assertEqual(tape[0]['dt'] // 8, 4)
        self.assertEqual(tape[0]['dt'] % 8, 0)

    def test_first_duration_need_not_align_to_the_step(self):
        """Pmove does not discard the remainder: a 20 ms command is two 8 ms
        steps and a 4 ms one, so the tape asks for 20, not 16 or 24."""
        tape = tape_entries([row(0, 120, 0, GROUND, pre_cmd_time=100)])
        self.assertEqual(tape[0]['dt'], 20)

    def test_duration_over_a_second_is_clamped_as_pmove_clamps_it(self):
        tape = tape_entries([row(0, 5000, 0, GROUND, pre_cmd_time=100)])
        self.assertEqual(tape[0]['dt'], 1000)

    def test_zero_duration_is_refused_not_given_a_step(self):
        with self.assertRaises(RecordingError) as caught:
            tape_entries([row(0, 100, 0, GROUND, pre_cmd_time=100)])
        self.assertIn('0 ms', str(caught.exception))

    def test_negative_duration_is_refused(self):
        with self.assertRaises(RecordingError):
            tape_entries([row(0, 100, 0, GROUND, pre_cmd_time=140)])

    def test_missing_first_pre_cmd_time_is_refused(self):
        first = row(0, 100, 0, GROUND)
        first['pre_cmd_time'] = ''
        with self.assertRaises(RecordingError):
            tape_entries([first])

    def test_missing_later_pre_cmd_time_falls_back_to_the_previous_command(self):
        second = row(1, 116, 16, GROUND)
        second['pre_cmd_time'] = ''
        tape = tape_entries([row(0, 100, 0, GROUND, pre_cmd_time=92), second])
        self.assertEqual([e['dt'] for e in tape], [8, 16])


class TapeTest(unittest.TestCase):
    def test_delta_is_the_accepted_pmove_duration(self):
        tape = tape_entries([row(0, 100, 0, GROUND, pre_cmd_time=92), row(1, 108, 8, GROUND),
                             row(2, 124, 24, GROUND, pre_cmd_time=108)])
        self.assertEqual([e['dt'] for e in tape], [8, 8, 16])

    def test_gap_is_preserved_not_normalised(self):
        """A dropped frame must replay as a gap; a nominal cadence would shift
        every later command to a different physics time."""
        tape = tape_entries([row(0, 100, 0, GROUND, pre_cmd_time=92),
                             row(1, 132, 32, GROUND, pre_cmd_time=100)])
        self.assertEqual(tape[1]['dt'], 32)

    def test_durations_chain_from_the_recorded_command_times(self):
        """Playback adds each delta to its own ps->commandTime, so the sum of
        the deltas walks the recorded command times exactly."""
        rows = [row(0, 100, 0, GROUND, pre_cmd_time=92), row(1, 132, 32, GROUND,
                pre_cmd_time=100), row(2, 140, 40, GROUND, pre_cmd_time=132)]
        tape = tape_entries(rows)
        when = 92
        for entry, source in zip(tape, rows):
            when += entry['dt']
            self.assertEqual(when, source['cmd_time'])

    def test_duration_is_not_the_server_elapsed_clock(self):
        """`elapsed_ms` is the recording's server-frame clock; several commands
        share one, and it is never a command's physics duration."""
        tape = tape_entries([row(0, 100, 0, GROUND, pre_cmd_time=92),
                             row(1, 108, 0, GROUND), row(2, 116, 0, GROUND)])
        self.assertEqual([e['dt'] for e in tape], [8, 8, 8])

    def test_angles_are_degrees(self):
        tape = tape_entries([row(0, 100, 0, GROUND, yaw=137.5)])
        self.assertAlmostEqual(tape[0]['yaw'], 137.5)


class ContractTest(unittest.TestCase):
    def test_transitions_are_changes_not_repeats(self):
        rows = [row(0, 100, 0, GROUND), row(1, 108, 8, GROUND), row(2, 116, 16, AIR),
                row(3, 124, 24, AIR), row(4, 132, 32, GROUND)]
        kinds = [p['kind'] for p in contract(rows) if p['kind'] != 'checkpoint']
        self.assertEqual(kinds, ['takeoff', 'landing'])

    def test_checkpoints_are_periodic(self):
        rows = [row(i, 100 + 8 * i, 8 * i, GROUND) for i in range(200)]
        marks = [p['elapsed_ms'] for p in contract(rows) if p['kind'] == 'checkpoint']
        self.assertEqual(marks[0], 0)
        self.assertTrue(all(b - a >= 500 for a, b in zip(marks, marks[1:])))

    def test_speed_is_horizontal(self):
        rows = [row(0, 100, 0, GROUND, speed=300.0)]
        self.assertAlmostEqual(contract(rows)[0]['speed'], 300.0)

    def test_every_mark_names_its_command(self):
        rows = [row(0, 100, 0, GROUND), row(1, 108, 8, GROUND), row(2, 116, 16, AIR),
                row(3, 124, 24, GROUND)]
        points = contract(rows)
        self.assertTrue(all('command' in p for p in points))
        transitions = [p for p in points if p['kind'] != 'checkpoint']
        self.assertEqual([p['kind'] for p in transitions], ['takeoff', 'landing'])
        self.assertEqual([p['command'] for p in transitions], [2, 3])

    def test_marks_in_one_server_frame_name_different_commands(self):
        """Four commands in one server frame share `elapsed_ms`. The server
        clock cannot tell them apart; the command index can, and a replay that
        read these marks by time would measure all four against the first."""
        rows = [row(0, 100, 0, GROUND), row(1, 108, 0, GROUND), row(2, 116, 0, AIR),
                row(3, 124, 0, GROUND)]
        points = contract(rows)
        self.assertEqual({p['elapsed_ms'] for p in points}, {0})
        transitions = [p for p in points if p['kind'] != 'checkpoint']
        self.assertEqual([p['command'] for p in transitions], [2, 3])

    def test_command_index_is_the_tape_index(self):
        rows = [row(i, 100 + 8 * i, 8 * i, GROUND) for i in range(6)]
        rows[2]['post_ground'] = AIR
        tape = tape_entries(rows)
        for point in contract(rows):
            self.assertEqual(tape[point['command']]['source_row'], rows[point['command']]['row'])

    def test_command_indices_skip_rows_without_a_command(self):
        rows = [row(0, 100, 0, GROUND), row(1, 108, 8, GROUND)]
        rows[0]['cmd_time'] = ''
        self.assertEqual(command_indices(rows), {1: 0})


class TapeTextTest(unittest.TestCase):
    def test_tape_is_the_versioned_format(self):
        source = write([row(0, 100, 0, GROUND, pre_cmd_time=68), row(1, 108, 8, AIR)])
        text = tape_text(build(source))
        self.assertEqual(text.split('\n')[0], TAPE_FORMAT)
        self.assertEqual(TAPE_FORMAT, 'BOTROUTE2')
        self.assertNotIn('BOTROUTE1', text)

    def test_first_command_line_carries_its_duration(self):
        source = write([row(0, 100, 0, GROUND, pre_cmd_time=68), row(1, 108, 8, GROUND)])
        commands = [l for l in tape_text(build(source)).split('\n') if l.startswith('c ')]
        self.assertEqual(commands[0].split()[1], '32')
        self.assertEqual(commands[1].split()[1], '8')

    def test_mark_line_carries_its_command_index(self):
        source = write([row(0, 100, 0, GROUND, pre_cmd_time=92), row(1, 108, 8, AIR)])
        marks = [l for l in tape_text(build(source)).split('\n') if l.startswith('k ')]
        self.assertTrue(marks)
        for line in marks:
            fields = line.split()
            self.assertEqual(len(fields), 8)        # k command elapsed x y z speed kind
            self.assertGreaterEqual(int(fields[1]), 0)


class BuildTest(unittest.TestCase):
    def test_asset_carries_binding_start_and_tape(self):
        source = write([row(0, 100, 0, GROUND, pre_cmd_time=92), row(1, 108, 8, AIR)])
        asset = build(source)
        self.assertEqual(asset['format'], FORMAT)
        self.assertEqual(asset['tape']['count'], 2)
        self.assertEqual(asset['start']['origin'], [10.0, 20.0, 30.0])
        self.assertIs(asset['start']['ground'], True)
        self.assertEqual(asset['physics']['pmove_msec'], 8)
        self.assertEqual(len(asset['binding']['recording_sha256']), 64)

    def test_duration_includes_the_first_command(self):
        source = write([row(0, 100, 0, GROUND, pre_cmd_time=68), row(1, 108, 8, GROUND)])
        self.assertEqual(build(source)['tape']['duration_ms'], 40)

    def test_asset_is_unapproved_on_build(self):
        source = write([row(0, 100, 0, GROUND)])
        self.assertEqual(build(source)['approval']['state'], 'unapproved')

    def test_asset_is_json_serialisable(self):
        source = write([row(0, 100, 0, GROUND), row(1, 108, 8, AIR)])
        json.dumps(build(source), allow_nan=False)


class BotSubjectTest(unittest.TestCase):
    """A bot recording is armed by the corner fixture on the release frame, so
    its row 0 is the release frame's own command and the bot may not have moved
    yet. Nothing in the converter may treat that row as absent: the command was
    given a real Pmove duration, and dropping it or nominalising it shifts every
    later command to a different physics time."""

    def release_row(self, duration=32, elapsed=25):
        """Row 0 of a bot recording: the release frame's command, which the bot
        left at rest, with the duration Pmove actually ran for it."""
        first = row(0, 100, elapsed, GROUND, speed=0.0, pre_cmd_time=100 - duration)
        first.update(forward='0', right='0', up='0', post_x='10')
        return first

    def test_release_row_keeps_its_real_duration(self):
        for duration in (8, 25, 32):
            tape = tape_entries([self.release_row(duration)])
            self.assertEqual(tape[0]['dt'], duration,
                             'release command of %d ms' % duration)

    def test_zero_input_release_row_is_still_a_command(self):
        """The release frame is part of the trip whether or not it moves the
        body. A tape that starts one command later starts somewhere else."""
        tape = tape_entries([self.release_row(), row(1, 108, 33, GROUND)])
        self.assertEqual(len(tape), 2)
        self.assertEqual(tape[0]['forward'], 0)
        self.assertEqual(tape[0]['up'], 0)
        self.assertEqual(tape[0]['source_row'], 0)
        self.assertEqual(command_indices([self.release_row(), row(1, 108, 33, GROUND)])[0], 0)

    def test_release_row_chains_the_next_command(self):
        """The second command's duration is its own, not the gap from a root
        the recorder never wrote."""
        tape = tape_entries([self.release_row(32), row(1, 108, 33, GROUND)])
        self.assertEqual([entry['dt'] for entry in tape], [32, 8])

    def test_built_asset_starts_at_the_release_state(self):
        """A bot recording's nonzero first `elapsed_ms` is the release-to-first-
        command offset and is not a missing root: the start state is row 0's own
        pre-Pmove snapshot, and the duration counts the release command."""
        source = write([self.release_row(32), row(1, 108, 33, GROUND)],
                       name='route-asset-build-bot')
        asset = build(source)
        self.assertEqual(asset['tape']['count'], 2)
        self.assertEqual(asset['tape']['duration_ms'], 40)
        self.assertEqual(asset['start']['origin'], [10.0, 20.0, 30.0])
        self.assertIs(asset['start']['ground'], True)


class CommandMsTest(unittest.TestCase):
    def test_rowless_command_time_is_skipped(self):
        blank = row(0, 100, 0, GROUND)
        blank['cmd_time'] = ''
        self.assertIsNone(command_ms(blank, None))


if __name__ == '__main__':
    unittest.main()
