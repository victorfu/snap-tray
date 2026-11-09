"""Exercise macOS SDK selection with inert build/deploy tools, on any Bash host."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
GIT_BASH = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe"
BASH = str(GIT_BASH) if sys.platform == "win32" and GIT_BASH.exists() else shutil.which("bash")
BUILD_ENTRIES = ("build.sh", "build-release.sh", "run-tests.sh",
                 "build-and-run.sh", "build-and-run-release.sh")


def executable(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#!/bin/bash\n" + text, encoding="utf-8", newline="\n")
    path.chmod(0o755)


@unittest.skipUnless(BASH, "Bash is required")
class MacQtSelectionTests(unittest.TestCase):
    def run_fixture(self, version="6.12.0", missing=None, entry=None):
        with tempfile.TemporaryDirectory(prefix="snaptray mac qt ") as directory:
            root = Path(directory)
            qt = root / "selected SDK"
            tools = root / "tools"
            sdk = root / "macOS SDK"
            sdk.mkdir()
            sparkle = root / "Sparkle.framework"
            sparkle.mkdir()
            config = qt / "lib/cmake/Qt6/Qt6Config.cmake"
            config.parent.mkdir(parents=True)
            config.touch()
            executable(qt / "bin/qmake", f"printf '%s\\n' '{version}'\n")
            executable(qt / "bin/macdeployqt", 'printf "%s\\n" "$@" > "$TEST_DEPLOY_LOG"\n')
            executable(tools / "brew", 'echo "Unexpected Homebrew selection" >&2\nexit 99\n')
            executable(tools / "xcrun", 'printf "%s\\n" "$MACOS_SDK_PATH"\n')
            executable(tools / "cmake", 'printf "%s\\n" "$@" >> "$TEST_CMAKE_LOG"\n'
                       'if [ "$1" = "--build" ]; then mkdir -p "$TEST_APP_PATH"; fi\n')
            if missing and missing != "root":
                (qt / missing).unlink()
            env = os.environ.copy()
            env.update(QT_ROOT_DIR=qt.as_posix(), MACOS_SDK_PATH=sdk.as_posix(),
                       SPARKLE_FRAMEWORK=sparkle.as_posix(),
                       SNAPTRAY_SPARKLE_FEED_URL="https://example.invalid/appcast.xml",
                       SNAPTRAY_SPARKLE_PUBLIC_KEY="test-key", CODESIGN_IDENTITY="",
                       TEST_CMAKE_LOG=(root / "cmake.log").as_posix(),
                       TEST_DEPLOY_LOG=(root / "deploy.log").as_posix(),
                       TEST_APP_PATH=(root / "build/bin/SnapTray.app").as_posix(),
                       OSTYPE="darwin", PATH=str(tools) + os.pathsep + env["PATH"])
            env.pop("BASH_ENV", None)
            if missing == "root":
                env.pop("QT_ROOT_DIR", None)
            if entry:
                source = (ROOT / "scripts" / entry).read_text(encoding="utf-8")
                source = source.split('configure_build_dir "$BUILD_DIR"', 1)[0]
                source += '\nconfigure_build_dir "$BUILD_DIR" build Debug\n'
                script = root / "scripts" / entry
            else:
                source = (ROOT / "packaging/macos/package.sh").read_text(encoding="utf-8")
                # Exercise selection, CMake invocation and deployment; stop
                # before any bundle cleanup, signing, notarization or publishing.
                source = source.split('echo "Removing unused optional Qt modules..."', 1)[0]
                script = root / "packaging/macos/package.sh"
                (root / "CMakeLists.txt").write_text("project(SnapTray VERSION 1.0.0 LANGUAGES CXX)\n")
            script.parent.mkdir(parents=True)
            script.write_text(source, encoding="utf-8", newline="\n")
            result = subprocess.run([BASH, str(script)], env=env, capture_output=True,
                                    text=True, timeout=20)
            cmake = (root / "cmake.log").read_text(encoding="utf-8") if (root / "cmake.log").exists() else ""
            deploy = (root / "deploy.log").read_text(encoding="utf-8") if (root / "deploy.log").exists() else ""
            return result, cmake, deploy, qt.as_posix()

    def test_packaging_builds_and_deploys_from_selected_sdk(self):
        result, cmake, deploy, qt = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"-DCMAKE_PREFIX_PATH={qt}\n", cmake)
        self.assertIn(f"-DQt6_DIR:PATH={qt}/lib/cmake/Qt6\n", cmake)
        self.assertIn("-U\nQt6*_DIR\n-U\nQT_DIR\n", cmake)
        self.assertIn("SnapTray.app", deploy)

    def test_packaging_rejects_wrong_version_before_build(self):
        for version in ("6.11.2", "6.12.1"):
            with self.subTest(version=version):
                result, cmake, deploy, _ = self.run_fixture(version=version)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires Qt 6.12.0", result.stdout)
                self.assertEqual((cmake, deploy), ("", ""))

    def test_packaging_rejects_missing_sdk_or_tools(self):
        for missing in ("root", "bin/qmake", "bin/macdeployqt", "lib/cmake/Qt6/Qt6Config.cmake"):
            with self.subTest(missing=missing):
                result, cmake, deploy, _ = self.run_fixture(missing=missing)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Set QT_ROOT_DIR", result.stdout)
                self.assertEqual((cmake, deploy), ("", ""))

    def test_local_entries_honor_explicit_sdk_without_homebrew(self):
        for entry in BUILD_ENTRIES:
            with self.subTest(entry=entry):
                result, cmake, _, qt = self.run_fixture(entry=entry)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f"-DCMAKE_PREFIX_PATH={qt}\n", cmake)

    def test_ci_and_release_pin_the_same_sdk(self):
        for workflow in ("ci.yml", "release.yml"):
            job = (ROOT / ".github/workflows" / workflow).read_text(encoding="utf-8").split("  build-macos:", 1)[1]
            job = job.split("\n  build-", 1)[0]
            with self.subTest(workflow=workflow):
                self.assertIn("version: '6.12.0'", job)
                self.assertIn("host: 'mac'", job)
                self.assertIn("arch: 'clang_64'", job)
                self.assertIn("modules: 'qtimageformats'", job)
                self.assertNotIn("brew install qt", job)


if __name__ == "__main__":
    unittest.main()
