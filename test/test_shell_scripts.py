import subprocess
import unittest
from pathlib import Path


TEST_DIR = Path(__file__).resolve().parent
SHELL_TESTS = sorted(TEST_DIR.glob("*.sh"))


class ShellScriptTests(unittest.TestCase):
    def test_shell_test_scripts_exist(self):
        self.assertTrue(SHELL_TESTS, "No shell test scripts found")


def make_shell_test(script):
    def run_script(self):
        subprocess.run(["sh", str(script)], check=True)

    return run_script


for shell_test in SHELL_TESTS:
    test_name = "test_" + shell_test.stem.removeprefix("test_")
    setattr(ShellScriptTests, test_name, make_shell_test(shell_test))


if __name__ == "__main__":
    unittest.main(verbosity=2)
