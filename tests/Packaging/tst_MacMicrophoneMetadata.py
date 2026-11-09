#!/usr/bin/env python3

import plistlib
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise AssertionError(message)


def load_plist(path: Path) -> dict:
    with path.open("rb") as plist_file:
        return plistlib.load(plist_file)


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: tst_MacMicrophoneMetadata.py <project-root>")

    project_root = Path(sys.argv[1])
    info_plist = load_plist(project_root / "cmake" / "Info.plist.in")
    entitlements = load_plist(
        project_root / "packaging" / "macos" / "entitlements.plist"
    )
    package_script = (
        project_root / "packaging" / "macos" / "package.sh"
    ).read_text(encoding="utf-8")

    usage_description = info_plist.get("NSMicrophoneUsageDescription")
    if not isinstance(usage_description, str) or not usage_description.strip():
        fail("NSMicrophoneUsageDescription must be a non-empty string")

    if entitlements.get("com.apple.security.device.audio-input") is not True:
        fail("com.apple.security.device.audio-input must be true")

    if 'codesign_runtime "$APP_PATH" --deep' in package_script:
        fail("top-level app signing must not propagate app entitlements with --deep")


if __name__ == "__main__":
    main()
