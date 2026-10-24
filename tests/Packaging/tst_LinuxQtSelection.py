"""Exercise Linux packaging SDK selection without building or deploying."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]


def executable(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#!/bin/bash\n" + text)
    path.chmod(0o755)


class LinuxQtSelectionTests(unittest.TestCase):
    def run_fixture(self, version="6.12.0", missing=False):
        with tempfile.TemporaryDirectory(prefix="snaptray linux qt ") as directory:
            root = Path(directory)
            qt = root / "selected SDK"
            tools = root / "tools"
            config = qt / "lib/cmake/Qt6/Qt6Config.cmake"
            config.parent.mkdir(parents=True)
            if not missing:
                config.touch()
            executable(qt / "bin/qmake", f"echo {version}\n")
            executable(tools / "cmake", 'printf "%s\\n" "$@" > "$TEST_CMAKE_LOG"\n'
                       'printf "%s\\n" "$QMAKE" > "$TEST_QMAKE_LOG"\n')
            for tool in ("xvfb-run", "xauth"):
                executable(tools / tool, "exit 0\n")
            script = root / "packaging/linux/package-appimage.sh"
            script.parent.mkdir(parents=True)
            shutil.copy(ROOT / "packaging/linux/appimage-helpers.sh", script.parent)
            source = (ROOT / "packaging/linux/package-appimage.sh").read_text()
            script.write_text(source.split('cmake --build "$BUILD_DIR"', 1)[0])
            (root / "CMakeLists.txt").write_text("project(SnapTray VERSION 1.0.0 LANGUAGES CXX)\n")
            env = os.environ.copy()
            env.update(QT_ROOT_DIR=str(qt), PATH=str(tools) + os.pathsep + env["PATH"],
                       TEST_CMAKE_LOG=str(root / "cmake.log"),
                       TEST_QMAKE_LOG=str(root / "qmake.log"))
            env.pop("BASH_ENV", None)
            result = subprocess.run(["bash", str(script)], env=env, capture_output=True,
                                    text=True, timeout=20)
            cmake = (root / "cmake.log").read_text() if (root / "cmake.log").exists() else ""
            qmake = (root / "qmake.log").read_text() if (root / "qmake.log").exists() else ""
            return result, cmake, qmake, str(qt)

    def test_build_and_deployment_select_same_sdk(self):
        result, cmake, qmake, qt = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"-DCMAKE_PREFIX_PATH={qt}\n", cmake)
        self.assertIn(f"-DQt6_DIR:PATH={qt}/lib/cmake/Qt6\n", cmake)
        self.assertIn("-U\nQt6*_DIR\n-U\nQT_DIR\n", cmake)
        self.assertEqual(qmake, f"{qt}/bin/qmake\n")

    def test_wrong_version_is_rejected_before_build(self):
        for version in ("6.11.2", "6.12.1"):
            with self.subTest(version=version):
                result, cmake, qmake, _ = self.run_fixture(version=version)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires Qt 6.12.0", result.stderr)
                self.assertEqual((cmake, qmake), ("", ""))

    def test_incomplete_sdk_is_rejected_before_build(self):
        result, cmake, qmake, _ = self.run_fixture(missing=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Set QT_ROOT_DIR", result.stderr)
        self.assertEqual((cmake, qmake), ("", ""))


if __name__ == "__main__":
    unittest.main()
