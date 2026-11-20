import unittest

import renderer_bench as rb

LOG = """\
2026-10-08 20:00:01 frame:100 all:  4 sv:  0 ev:  0 cl:  1 gm:  0 rf:  1 bk:  2
2026-10-08 20:00:01 frame:101 all:  3 sv:  0 ev:  0 cl:  1 gm:  0 rf:  1 bk:  1
2026-10-08 20:00:01 frame:102 all: 40 sv:  0 ev:  0 cl:  1 gm:  0 rf: 30 bk:  9
2026-10-08 20:00:02 frame:103 all:  5 sv:  0 ev:  0 cl:  1 gm:  0 rf:  2 bk:  2
2026-10-08 20:00:02 4210 frames, 12.3 seconds: 342.3 fps
"""


class ParseTest(unittest.TestCase):
    def test_frames_and_summary(self):
        frames, summary = rb.parse_log(LOG)
        self.assertEqual(len(frames), 4)
        self.assertEqual(frames[2][0], 40)
        self.assertEqual(summary, {'frames': 4210, 'seconds': 12.3, 'fps': 342.3})

    def test_playback_frames_only(self):
        menu = '2026-10-08 20:00:03 frame:104 all:  90 sv:  0 ev:  0 cl:  1 gm:  0 rf:  1 bk:  2\n'
        log = LOG.replace('4210 frames', '3 frames') + menu
        frames, summary = rb.parse_log(log)
        self.assertEqual(summary['frames'], 3)
        self.assertEqual([f[0] for f in frames], [3, 40, 5])   # loading before and menu after dropped

    def test_stats(self):
        frames, summary = rb.parse_log(LOG)
        s = rb.frame_stats(frames, summary, wall_s=20.0)
        self.assertEqual(s['max_ms'], 40)
        self.assertEqual(s['over_33'], 1)
        self.assertEqual(s['over_50'], 0)
        self.assertAlmostEqual(s['startup_and_load_s'], 7.7)
        self.assertAlmostEqual(s['rf_mean_ms'], 8.5)

    def test_cfg_merges_family_settings(self):
        settings = {'common': {'r_swapInterval': '0'},
                    'scenarios': {'max': {'by_family': {'rend2': {'r_hdr': '1'}}}}}
        cand = {'name': 'x', 'family': 'rend2', 'renderer': 'rd-rend2'}
        sets = rb.bench_settings(cand, 'max', settings)
        self.assertEqual(sets, {'r_swapInterval': '0', 'r_hdr': '1', 'cl_renderer': 'rd-rend2'})
        cfg = rb.bench_cfg(cand, 'max', settings, 'bench', None)
        self.assertIn('set com_speeds 1', cfg)
        shots = rb.bench_cfg(cand, 'max', settings, 'bench', [10, 30])
        self.assertIn('wait 10\nscreenshot shot00010\nwait 20\nscreenshot shot00030', shots)

    def test_presentmon_csv_keeps_playback(self):
        import os, tempfile
        rows = ['Application,ProcessID,PresentRuntime,PresentMode,FrameTime,GPUTime']
        rows += ['openjk.x86.exe,1,Other,Hardware: Legacy Flip,500.0,1.0'] * 3          # loading
        rows += ['openjk.x86.exe,1,Other,Hardware: Legacy Flip,%s,0.5' % v for v in ('2.0', '3.0', '40.0', '2.0')]
        fd, path = tempfile.mkstemp(suffix='.csv')
        with os.fdopen(fd, 'w') as f:
            f.write('\n'.join(rows) + '\n')
        try:
            s = rb.parse_presentmon(path, playback_frames=4)
        finally:
            os.remove(path)
        self.assertEqual(s['pm_frames'], 4)
        self.assertEqual(s['pm_max_ms'], 40.0)
        self.assertEqual(s['pm_over_33'], 1)
        self.assertEqual(s['pm_gpu_mean_ms'], 0.5)
        self.assertIsNone(rb.parse_presentmon(path))

    def test_fixtures_are_consistent(self):
        families = {c['family'] for c in rb.candidates()}
        scen = rb.load('scenarios.json')
        self.assertIn('default', scen['scenarios'])
        for fam in scen['scenarios']['max']['by_family']:
            self.assertIn(fam, families)


if __name__ == '__main__':
    unittest.main()
