"""Exercise NSIS signing and wrapper exit codes without certificates or builds."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == "win32", "cmd.exe is required")
class NsisSigningTests(unittest.TestCase):
    def test_signing_results(self):
        source = (ROOT / "packaging/windows/package-nsis.bat").read_text()
        # Keep the signing/installer control flow intact. Skip build/deployment,
        # and CALL inert batch tools so they return like the real executables.
        tail = source[source.index("REM Step 4: Code signing"):]
        tail = tail.replace("signtool sign", "call signtool.bat sign")
        tail = tail.replace("makensis /V3", "call makensis.bat /V3")
        cases = [(False, ""), (True, ""), (True, "SnapTray.exe"),
                 (True, "WinSparkle.dll"), (True, "SnapTray-1.2.3-Setup.exe")]
        for signed, failure in cases:
            for mode in ("nsis", ""):
                with self.subTest(signed=signed, failure=failure, mode=mode):
                    with tempfile.TemporaryDirectory(prefix="snaptray signing ") as directory:
                        folder = Path(directory)
                        shutil.copyfile(ROOT / "packaging/windows/package.bat", folder / "package.bat")
                        (folder / "package-nsis.bat").write_text(
                            '@echo off\nsetlocal enabledelayedexpansion\n'
                            'set "SCRIPT_DIR=%~dp0"\nset "PROJECT_ROOT=%~dp0"\n'
                            'set "STAGING_DIR=%~dp0staging"\nset "OUTPUT_DIR=%~dp0dist"\n'
                            'set APP_NAME=SnapTray\nset VERSION=1.2.3\n' + tail,
                            encoding="ascii")
                        (folder / "signtool.bat").write_text(
                            '@echo off\necho %*>>signatures.txt\n'
                            'set "target="\n:next\n'
                            'if "%~1"=="" goto done\nset "target=%~nx1"\nshift\ngoto next\n'
                            ':done\nif "%target%"=="%SIGNING_FAILURE%" exit /b 7\nexit /b 0\n',
                            encoding="ascii")
                        (folder / "makensis.bat").write_text(
                            '@echo nsis>>packages.txt\n@exit /b 0\n', encoding="ascii")
                        (folder / "package-msix.bat").write_text(
                            '@echo msix>>packages.txt\n@exit /b 0\n', encoding="ascii")
                        env = dict(os.environ, CODESIGN_CERT="test certificate.pfx" if signed else "",
                                   CODESIGN_PASSWORD="test password", SIGNING_FAILURE=failure)
                        result = subprocess.run(["cmd.exe", "/d", "/c", f"package.bat {mode}"],
                                                cwd=folder, env=env, capture_output=True, text=True, timeout=10)
                        output = result.stdout + result.stderr
                        self.assertEqual(result.returncode, 1 if failure else 0, output)
                        self.assertEqual("Build Complete" in output, not bool(failure), output)
                        if failure:
                            self.assertIn("ERROR:", output)
                        log = folder / "signatures.txt"
                        calls = log.read_text().splitlines() if log.exists() else []
                        targets = ["SnapTray.exe", "WinSparkle.dll", "SnapTray-1.2.3-Setup.exe"]
                        expected = targets[:targets.index(failure) + 1] if failure else targets
                        if not signed:
                            expected = []
                        self.assertEqual(len(calls), len(expected), output)
                        for call, target in zip(calls, expected):
                            self.assertIn("/fd SHA256", call)
                            self.assertTrue(call.endswith(target + '"'), call)
                            self.assertIn('/f "test certificate.pfx" /p "test password"', call)
                        log = folder / "packages.txt"
                        packages = log.read_text().splitlines() if log.exists() else []
                        expected_packages = [] if failure in targets[:2] else ["nsis"]
                        if not failure and not mode:
                            expected_packages.append("msix")
                        self.assertEqual(packages, expected_packages, output)


if __name__ == "__main__":
    unittest.main()
