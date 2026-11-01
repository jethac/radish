import pathlib
import subprocess
import tempfile
import unittest

CHECK = pathlib.Path(__file__).parents[1] / 'scripts' / 'check-gpu-log.py'

class GpuLogTest(unittest.TestCase):
    def check_logs(self, before, after):
        with tempfile.TemporaryDirectory() as d:
            a, b = pathlib.Path(d) / 'before', pathlib.Path(d) / 'after'
            a.write_text(before)
            b.write_text(after)
            return subprocess.run(['python3', str(CHECK), str(a), str(b)], capture_output=True, text=True)

    def test_old_fault_evicted_is_not_a_new_fault(self):
        result = self.check_logs('[10.0] gpu fault ring 0\n[20.0] audit old\n', '[20.0] audit old\n[21.0] audit new\n')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_new_fault_is_rejected(self):
        result = self.check_logs('[20.0] audit old\n', '[20.0] audit old\n[21.0] gpu fault ring 0\n')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('gpu fault ring 0', result.stderr)

    def test_timestamp_reset_is_rejected(self):
        result = self.check_logs('[200.0] audit old\n', '[2.0] audit new\n')
        self.assertEqual(result.returncode, 1, result.stderr)

    def test_unparseable_log_cannot_prove_safety(self):
        self.assertEqual(self.check_logs('', '').returncode, 1)

if __name__ == '__main__':
    unittest.main()
