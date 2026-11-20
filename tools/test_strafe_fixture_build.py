"""The build script's exit code, which a campaign trusts before anything else.

No compiler runs here: `subprocess.run` is replaced by a stub that writes what
a builder would write into the build log and leaves behind the binaries the
caller says it left behind. What is under test is what the script concludes
from that, because a build whose game DLL failed once reported success and only
the absent hash manifest kept it out of a campaign.
"""
import json
import shutil
import sys
import types
import unittest
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import strafe_fixture_build as build

ARTIFACTS = Path(__file__).resolve().parent.parent / '.codex-research/unit-artifacts'


class Builder(object):
    """A stub builder: what it leaves in RelWithDebInfo, and what it says."""

    def __init__(self, out, binaries, text='Build succeeded.\n', returncode=0):
        self.out = Path(out)
        self.binaries = binaries
        self.text = text
        self.returncode = returncode
        self.commands = []

    def __call__(self, command, cwd=None, stdout=None, stderr=None, env=None):
        self.commands.append(command)
        stage = self.out / 'RelWithDebInfo'
        stage.mkdir(parents=True, exist_ok=True)
        for name in self.binaries:
            (stage / name).write_bytes(name.encode())
        if stdout is not None:
            stdout.write(self.text)
            stdout.flush()
        return types.SimpleNamespace(returncode=self.returncode)


class ExitCodeTest(unittest.TestCase):
    def setUp(self):
        self.out = ARTIFACTS / 'fixture-build' / uuid.uuid4().hex
        self.real = build.subprocess.run
        self.argv = sys.argv

    def tearDown(self):
        build.subprocess.run = self.real
        sys.argv = self.argv
        # The stub build's own folder (a log, two stub binaries, a source manifest): one was left per test and run.
        shutil.rmtree(self.out, ignore_errors=True)

    def run_with(self, binaries, text='Build succeeded.\n'):
        build.subprocess.run = Builder(self.out, binaries, text)
        sys.argv = ['strafe_fixture_build.py', '--out', str(self.out)]
        return build.main()

    def manifests(self):
        return sorted(self.out.glob('*.sources.json'))

    def test_a_build_that_did_not_produce_the_game_dll_exits_non_zero(self):
        self.assertNotEqual(self.run_with(('openjkded.x86.exe',)), 0)
        self.assertEqual(self.manifests(), [],
                         'a build with no game DLL must not leave a manifest behind')

    def test_a_build_that_did_not_produce_the_engine_exits_non_zero(self):
        self.assertNotEqual(self.run_with(('jampgamex86.dll',)), 0)
        self.assertEqual(self.manifests(), [])

    def test_msbuild_failing_under_a_zero_exit_code_exits_non_zero(self):
        code = self.run_with(('openjkded.x86.exe', 'jampgamex86.dll'),
                             'jampgamex86.dll : fatal error LNK1104\nBuild FAILED.\n'
                             '    1 Error(s)\n')
        self.assertNotEqual(code, 0)
        self.assertEqual(self.manifests(), [])

    def test_a_complete_build_writes_its_manifest_and_exits_zero(self):
        self.assertEqual(self.run_with(('openjkded.x86.exe', 'jampgamex86.dll')), 0)
        manifests = self.manifests()
        self.assertEqual(len(manifests), 1)
        provenance = json.loads(manifests[0].read_text())
        self.assertEqual(sorted(provenance['binaries']), ['jampgamex86.dll', 'openjkded.x86.exe'])
        self.assertTrue(provenance['sources'])

    def test_a_builder_that_reports_failure_stops_before_the_manifest(self):
        build.subprocess.run = Builder(self.out, (), 'error\n', returncode=1)
        sys.argv = ['strafe_fixture_build.py', '--out', str(self.out)]
        self.assertNotEqual(build.main(), 0)
        self.assertEqual(self.manifests(), [])


class OutputCheckTest(unittest.TestCase):
    def test_missing_binaries_names_what_is_absent(self):
        out = ARTIFACTS / 'fixture-build' / uuid.uuid4().hex
        (out / 'RelWithDebInfo').mkdir(parents=True)
        self.addCleanup(shutil.rmtree, out, True)
        (out / 'RelWithDebInfo' / 'openjkded.x86.exe').write_bytes(b'engine')
        self.assertEqual(build.missing_binaries(out), ['jampgamex86.dll'])

    def test_a_build_with_both_binaries_is_missing_nothing(self):
        out = ARTIFACTS / 'fixture-build' / uuid.uuid4().hex
        (out / 'RelWithDebInfo').mkdir(parents=True)
        self.addCleanup(shutil.rmtree, out, True)
        for name in build.BINARIES:
            (out / 'RelWithDebInfo' / name).write_bytes(b'x')
        self.assertEqual(build.missing_binaries(out), [])

    def test_msbuilds_own_verdict_line_is_the_failure(self):
        self.assertTrue(build.build_failed('foo\nBuild FAILED.\n    1 Error(s)\n'))
        self.assertFalse(build.build_failed('Build succeeded.\n    0 Error(s)\n'))
        self.assertFalse(build.build_failed('note: Build FAILED. is what it prints\n'))


if __name__ == '__main__':
    unittest.main()
