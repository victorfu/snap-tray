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

    def test_existing_cache_selects_new_qt(self):
        for entry in ENTRIES:
            for old_available in (True, False):
                with self.subTest(entry=entry, old_available=old_available):
                    with QtCacheFixture(entry) as fixture:
                        fixture.seed(fixture.old_qt)
                        if not old_available:
                            fixture.config(fixture.old_qt).unlink()
                        fixture.run_entry()
                        fixture.assert_selection(self, fixture.new_qt)
                        self.assertTrue(fixture.pending.exists())
                        self.assertEqual(fixture.cache()["PRESERVED_OPTION"], "keep me")

    def test_equivalent_cache_is_not_reconfigured(self):
        for entry in ENTRIES:
            with self.subTest(entry=entry), QtCacheFixture(entry) as fixture:
                fixture.seed(fixture.new_qt)
                # Windows path spelling differences do not imply a new SDK.
                fixture.run_entry(str(fixture.new_qt).upper().replace("\\", "/") + "/.")
                self.assertEqual(fixture.configure_count(), 1)
                self.assertFalse(fixture.pending.exists())

    def test_missing_package_cache_and_build_type_change(self):
        for change in ("missing-qt-dir", "build-type"):
            with self.subTest(change=change), QtCacheFixture("build.bat") as fixture:
                fixture.seed(fixture.new_qt, build_type="Release" if change == "build-type" else "Debug")
                if change == "missing-qt-dir":
                    cache = fixture.build / "CMakeCache.txt"
                    lines = []
                    for line in cache.read_text().splitlines():
                        if line.startswith("Qt6_DIR:"):
                            # CMake requires a cache entry after its help comments.
                            while lines and lines[-1].startswith("//"):
                                lines.pop()
                        else:
                            lines.append(line)
                    cache.write_text("\n".join(lines) + "\n", encoding="utf-8")
                fixture.run_entry()
                fixture.assert_selection(self, fixture.new_qt)
                self.assertEqual(fixture.configure_count(), 2)

    def test_failed_configuration_is_retried(self):
        with QtCacheFixture("build.bat") as fixture:
            fixture.seed(fixture.old_qt)
            config = fixture.config(fixture.new_qt)
            valid_config = config.read_text()
            config.write_text('message(FATAL_ERROR "injected configure failure")\n')
            result = fixture.run_entry(expect_success=False)
            self.assertIn("injected configure failure", result.stdout + result.stderr)
            self.assertTrue(fixture.pending.exists())
            config.write_text(valid_config)
            fixture.run_entry()
            fixture.assert_selection(self, fixture.new_qt)
            self.assertTrue(fixture.pending.exists())
            # A successful configure alone does not finish runtime deployment.
            self.assertEqual(fixture.configure_count(), 2)
            fixture.pending.unlink()  # Simulate completed deployment for the no-op check.
            fixture.run_entry()
            self.assertEqual(fixture.configure_count(), 2)

    def test_invalid_selected_sdk_is_rejected(self):
        with QtCacheFixture("build.bat") as fixture:
            fixture.seed(fixture.old_qt)
            fixture.config(fixture.new_qt).unlink()
            fixture.run_entry(expect_success=False)
            self.assertEqual(fixture.configure_count(), 1)

    def test_pending_deployment_is_not_skipped_for_existing_dlls(self):
        with QtCacheFixture("build-release.bat") as fixture:
            fixture.seed(fixture.new_qt)
            binary_dir = fixture.build / "bin"
            binary_dir.mkdir()
            (binary_dir / "SnapTray.exe").touch()
            for module in ("Core", "Gui", "Widgets", "Qml", "Quick", "QuickWidgets"):
                (binary_dir / f"Qt6{module}.dll").touch()
            for folder in ("platforms", "qml/QtQuick/Layouts", "qml/QtQuick/Controls/Basic"):
                (binary_dir / folder).mkdir(parents=True, exist_ok=True)
            (binary_dir / "platforms/qwindows.dll").touch()
            args = ["cmake", f"-DSNAPTRAY_BUILD_DIR={fixture.build}", "-DSNAPTRAY_BUILD_TYPE=Release",
                    "-P", str(fixture.scripts / "deploy-windows-qt.cmake")]
            env = dict(os.environ, QT_PATH=str(fixture.new_qt))
            # No deployment is needed when the matching runtime is already present.
            subprocess.run(args, env=env, check=True, capture_output=True, timeout=15)
            fixture.pending.write_text("pending")
            # The new SDK deliberately has no deployment tool. Existing DLLs must
            # not bypass the pending update, and failure must retain the marker.
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Qt deployment tool not found", result.stdout + result.stderr)
            self.assertTrue(fixture.pending.exists())


class QtCacheFixture:
    def __init__(self, entry):
        self.entry = entry
        self.temp = tempfile.TemporaryDirectory(prefix="snaptray qt cache ")
        self.root = Path(self.temp.name)
        self.scripts = self.root / "scripts"
        self.scripts.mkdir()
        self.build_type = "Release" if "release" in entry else "Debug"
        self.build = self.root / ("release" if self.build_type == "Release" else "build")
        self.pending = self.build / ".qt-deploy-pending"
        self.old_qt = self.root / "Qt 6.8.0"
        self.new_qt = self.root / "Qt 6.10.1"
        for qt in (self.old_qt, self.new_qt):
            self.config(qt).parent.mkdir(parents=True)
            self.config(qt).write_text('find_package(Qt6Core CONFIG REQUIRED)\nset(Qt6_FOUND TRUE)\n')
            core = qt / "lib/cmake/Qt6Core/Qt6CoreConfig.cmake"
            core.parent.mkdir()
            core.write_text('set(Qt6Core_FOUND TRUE)\n')
        (self.root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.16)
project(QtCacheFixture LANGUAGES NONE)
find_package(QT NAMES Qt6 CONFIG REQUIRED)
find_package(Qt6 CONFIG REQUIRED)
file(APPEND "${CMAKE_BINARY_DIR}/configure-runs.txt" "configured\\n")
''')
        for helper in ("detect-qt.bat", "configure-windows.cmake", "deploy-windows-qt.cmake"):
            source = ROOT / "scripts" / helper
            if source.exists():
                (self.scripts / helper).write_bytes(source.read_bytes())
        setup = (ROOT / "scripts" / entry).read_text(encoding="utf-8").split("REM Build all targets", 1)[0]
        (self.scripts / entry).write_text(setup + "\nexit /b 0\n", encoding="ascii")

    @staticmethod
    def config(qt):
        return qt / "lib/cmake/Qt6/Qt6Config.cmake"

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.temp.cleanup()

    def seed(self, qt, build_type=None):
        subprocess.run(["cmake", "-S", str(self.root), "-B", str(self.build), "-G", "Ninja",
                        f"-DCMAKE_BUILD_TYPE={build_type or self.build_type}", f"-DCMAKE_PREFIX_PATH={qt}",
                        "-DPRESERVED_OPTION:STRING=keep me"], check=True, capture_output=True, timeout=20)

    def run_entry(self, qt_path=None, expect_success=True):
        # The fixture has no compiler; supply the real MSVC-generated prefix so
        # these cases exercise cache selection rather than the language check.
        with (self.build / "CMakeFiles/rules.ninja").open("a") as rules:
            rules.write("\nmsvc_deps_prefix = Note: including file:\n")
        env = dict(os.environ, QT_PATH=qt_path or str(self.new_qt))
        result = subprocess.run(["cmd.exe", "/d", "/c", self.entry], cwd=self.scripts,
                                env=env, capture_output=True, text=True, timeout=30)
        if (result.returncode == 0) != expect_success:
            raise AssertionError(result.stdout + result.stderr)
        return result

    def configure_count(self):
        return len((self.build / "configure-runs.txt").read_text().splitlines())

    def cache(self):
        result = {}
        for line in (self.build / "CMakeCache.txt").read_text().splitlines():
            if "=" in line and not line.startswith(("#", "//")):
                key, value = line.split("=", 1)
                result[key.split(":", 1)[0]] = value
        return result

    def assert_selection(self, test, qt):
        cache = self.cache()
        for key, folder in (("QT_DIR", "Qt6"), ("Qt6_DIR", "Qt6"), ("Qt6Core_DIR", "Qt6Core")):
            test.assertEqual(Path(cache[key]).resolve(), (qt / "lib/cmake" / folder).resolve(), key)
        test.assertEqual(Path(cache["CMAKE_PREFIX_PATH"]).resolve(), qt.resolve())
        test.assertEqual(cache["CMAKE_BUILD_TYPE"], self.build_type)


if __name__ == "__main__":
    unittest.main()
