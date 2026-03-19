#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: tst_generate_appcast.py <generator-path>")

    generator = Path(sys.argv[1]).resolve()
    if not generator.is_file():
        raise SystemExit(f"generator not found: {generator}")

    with tempfile.TemporaryDirectory() as tmpdir:
        output_path = Path(tmpdir) / "appcast.xml"
        subprocess.run(
            [
                sys.executable,
                str(generator),
                "--platform",
                "macos",
                "--version",
                "1.2.3",
                "--download-url",
                "https://example.com/SnapTray.zip",
                "--signature",
                "fake-signature",
                "--length",
                "12345",
                "--release-notes-url",
                "https://example.com/release-notes",
                "--minimum-system-version",
                "14.0.0",
                "--output",
                str(output_path),
            ],
            check=True,
        )

        root = ET.parse(output_path).getroot()
        item = root.find("./channel/item")
        if item is None:
            raise AssertionError("appcast item missing")

        minimum_system = item.find(f"{{{SPARKLE_NS}}}minimumSystemVersion")
        if minimum_system is None:
            raise AssertionError("minimum system version must be an item child")
        if minimum_system.text != "14.0.0":
            raise AssertionError(f"unexpected minimum system version: {minimum_system.text!r}")

        enclosure = item.find("enclosure")
        if enclosure is None:
            raise AssertionError("enclosure missing")
        if f"{{{SPARKLE_NS}}}minimumSystemVersion" in enclosure.attrib:
            raise AssertionError("minimum system version must not be encoded as an enclosure attribute")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
