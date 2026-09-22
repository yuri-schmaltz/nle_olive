"""Regression checks for false-positive E2E runner results using real CTest."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("run_e2e", Path(__file__).with_name("run_e2e.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        original = runner.ROOT
        self.addCleanup(setattr, runner, "ROOT", original)
        runner.ROOT = Path(self.directory.name)
        self.build = runner.ROOT / "build-linux-release"

    def run_suite(self):
        with contextlib.redirect_stderr(io.StringIO()):
            return runner.run_e2e("linux-release")

    def test_missing_preset_does_not_use_other_build(self):
        alternate = runner.ROOT / "build"
        alternate.mkdir()
        (alternate / "CTestTestfile.cmake").write_text("")
        self.assertNotEqual(self.run_suite(), 0)

    def test_empty_inventory_is_failure(self):
        self.build.mkdir()
        (self.build / "CTestTestfile.cmake").write_text("")
        self.assertNotEqual(self.run_suite(), 0)

    def test_failed_test_propagates_and_writes_junit(self):
        # Generate a minimal real CTest project with a known failing E2E test.
        (runner.ROOT / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.21)\nproject(RunnerCheck NONE)\n'
            'enable_testing()\nadd_test(NAME failing COMMAND "${CMAKE_COMMAND}" -E false)\n'
            'set_tests_properties(failing PROPERTIES LABELS E2E)\n')
        subprocess.run(["cmake", "-S", str(runner.ROOT), "-B", str(self.build)],
                       check=True, capture_output=True)
        self.assertNotEqual(self.run_suite(), 0)
        reports = list((runner.ROOT / "qa-results").glob("e2e-*/junit.xml"))
        self.assertEqual(len(reports), 1)
        self.assertIn('<failure', reports[0].read_text())


if __name__ == "__main__":
    unittest.main()
