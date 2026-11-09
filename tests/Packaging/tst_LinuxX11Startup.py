#!/usr/bin/env python3
"""Exercise the real startup guard under Xvfb, including catalog environments."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


COMMAND_TIMEOUT = 10
STARTUP_SECONDS = 3
RUNTIME_ERROR = "supports X11 sessions only"


def check_startup(binary: str) -> None:
    with tempfile.TemporaryDirectory(prefix="snaptray-x11-startup-") as temporary:
        root = Path(temporary)
        environment = os.environ.copy()
        for name in ("XDG_SESSION_TYPE", "WAYLAND_DISPLAY", "WAYLAND_SOCKET",
                     "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                     "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "LD_PRELOAD"):
            environment.pop(name, None)
        environment["QT_QPA_PLATFORM"] = "xcb"
        # Isolate settings, autostart files, caches, and single-instance IPC.
        for name in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME",
                     "XDG_RUNTIME_DIR", "TMPDIR"):
            directory = root / name
            directory.mkdir(mode=0o700)
            environment[name] = str(directory)
        environment["DBUS_SESSION_BUS_ADDRESS"] = f"unix:path={root}/no-session-bus"

        for session in (None, "tty", "unspecified", "x11"):
            env = environment.copy()
            if session is not None:
                env["XDG_SESSION_TYPE"] = session
            result = subprocess.run(
                [binary, "config", "--list"], env=env, capture_output=True,
                text=True, timeout=COMMAND_TIMEOUT)
            if result.returncode != 0 or "Current settings:" not in result.stdout:
                raise RuntimeError(f"X11 startup rejected session {session!r}:\n"
                                   f"{result.stdout}{result.stderr}")

        for overrides in (
            {"XDG_SESSION_TYPE": "wayland"},
            {"WAYLAND_DISPLAY": "wayland-0"},
            {"WAYLAND_SOCKET": "3"},
            {"XDG_SESSION_TYPE": "tty", "WAYLAND_DISPLAY": "wayland-0"},
            {"XDG_SESSION_TYPE": "x11", "QT_QPA_PLATFORM": "offscreen"},
        ):
            result = subprocess.run(
                [binary, "config", "--list"], env=environment | overrides,
                capture_output=True, text=True, timeout=COMMAND_TIMEOUT)
            if result.returncode != 1 or RUNTIME_ERROR not in result.stderr:
                raise RuntimeError(f"Unsupported environment was not rejected: {overrides}\n"
                                   f"{result.stdout}{result.stderr}")

        # The catalog launches the tray app without session metadata. Checking
        # --version alone bypasses the runtime guard and cannot catch this bug.
        with (root / "startup.log").open("w+") as log:
            process = subprocess.Popen(
                [binary], env=environment, stdout=log, stderr=subprocess.STDOUT)
            try:
                try:
                    process.wait(timeout=STARTUP_SECONDS)
                except subprocess.TimeoutExpired:
                    pass
                else:
                    log.seek(0)
                    raise RuntimeError(f"Tray app exited during X11 startup:\n{log.read()}")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=COMMAND_TIMEOUT)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == "__main__":
    check_startup(str(Path(sys.argv[1]).absolute()))
    print("Linux X11 startup passed without session metadata; unsupported sessions rejected.")
