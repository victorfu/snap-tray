"""Run each build entry's setup against isolated fake Qt installations."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
ENTRIES = ("build.bat", "build-release.bat", "build-and-run.bat", "build-and-run-release.bat", "run-tests.bat")


@unittest.skipUnless(sys.platform == "win32", "cmd.exe is required")
class QtDetectionTests(unittest.TestCase):
    def test_all_entries(self):
        cases = [
            (("6.10.1", "6.8.0", "6.7.0"), "", True),
            (("6.8.0", "6.7.0"), "", False),
            ((), "", False),
            (("6.10.1", "6.7.0"), "custom Qt", True),
        ]
        for versions, override, succeeds in cases:
            with tempfile.TemporaryDirectory(prefix="snaptray qt ") as directory:
                folder = Path(directory)
                scripts = folder / "scripts"
                scripts.mkdir()
                qt = folder / "Qt"
                for version in versions:
                    binary = qt / version / "msvc2022_64/bin/qmake.exe"
                    binary.parent.mkdir(parents=True)
                    binary.touch()
                detector = (ROOT / "scripts/detect-qt.bat").read_text().replace("C:\\Qt", "%SNAPTRAY_TEST_QT_ROOT%")
                (scripts / "detect-qt.bat").write_text(detector, encoding="ascii")
                for entry in ENTRIES:
                    with self.subTest(versions=versions, override=override, entry=entry):
                        source = (ROOT / "scripts" / entry).read_text(encoding="utf-8")
                        # Stop before configure/build/run; execute the real setup unmodified.
                        setup = source.split("REM Configure if needed", 1)[0]
                        (scripts / entry).write_text(setup + '\nif "%QT_PATH%"=="%SNAPTRAY_TEST_EXPECTED%" exit /b 0\nexit /b 23\n', encoding="ascii")
                        env = os.environ.copy()
                        env.pop("QT_PATH", None)
                        if override:
                            env["QT_PATH"] = str(folder / override)
                        env["SNAPTRAY_TEST_QT_ROOT"] = str(qt)
                        env["SNAPTRAY_TEST_EXPECTED"] = env.get("QT_PATH", str(qt / "6.10.1/msvc2022_64"))
                        result = subprocess.run(["cmd.exe", "/d", "/c", entry], cwd=scripts,
                                                env=env, capture_output=True, timeout=10)
                        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
