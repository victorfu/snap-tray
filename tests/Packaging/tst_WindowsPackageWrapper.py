"""Exercise the real wrapper with inert children, without building packages."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == "win32", "cmd.exe is required")
class PackageWrapperTests(unittest.TestCase):
    def test_child_exit_codes_and_order(self):
        with tempfile.TemporaryDirectory(prefix="snaptray package ") as directory:
            folder = Path(directory)
            shutil.copyfile(ROOT / "packaging/windows/package.bat", folder / "package.bat")
            for name in ("nsis", "msix"):
                (folder / f"package-{name}.bat").write_text(
                    f"@echo {name}>>calls.txt\n@exit /b %SNAPTRAY_TEST_{name.upper()}_EXIT%\n",
                    encoding="ascii",
                )
            cases = [
                ("nsis", 0, 9, 0, ["nsis"]),
                ("NSIS", 7, 0, 7, ["nsis"]),
                ("msix", 7, 0, 0, ["msix"]),
                ("MSIX", 0, 9, 9, ["msix"]),
                ("", 0, 0, 0, ["nsis", "msix"]),
                ("", 7, 0, 1, ["nsis"]),
                ("", 0, 9, 1, ["nsis", "msix"]),
            ]
            for mode, nsis, msix, expected, calls in cases:
                with self.subTest(mode=mode, nsis=nsis, msix=msix):
                    (folder / "calls.txt").write_text("", encoding="ascii")
                    (folder / "run.bat").write_text(
                        f"@echo off\ncmd /d /c exit 19\ncall package.bat {mode}\nexit /b %ERRORLEVEL%\n",
                        encoding="ascii",
                    )
                    env = dict(os.environ, SNAPTRAY_TEST_NSIS_EXIT=str(nsis), SNAPTRAY_TEST_MSIX_EXIT=str(msix))
                    result = subprocess.run(["cmd.exe", "/d", "/c", "run.bat"], cwd=folder,
                                            env=env, capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                    self.assertEqual((folder / "calls.txt").read_text().splitlines(), calls)


if __name__ == "__main__":
    unittest.main()
