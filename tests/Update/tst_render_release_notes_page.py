#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: tst_render_release_notes_page.py <renderer-path>")

    renderer = Path(sys.argv[1]).resolve()
    if not renderer.is_file():
        raise SystemExit(f"renderer not found: {renderer}")

    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir_path = Path(tmpdir)
        release_json = tmpdir_path / "release.json"
        output_path = tmpdir_path / "release-notes.html"

        release_json.write_text(
            json.dumps(
                {
                    "tag_name": "v1.2.3",
                    "name": "SnapTray 1.2.3",
                    "published_at": "2026-03-19T13:24:05Z",
                    "html_url": "https://github.com/example/release/v1.2.3",
                    "body": "## What's Changed\n- Added **auto update** support\n- Visit [docs](https://example.com/docs)\n\nUse `SnapTray` daily.",
                }
            ),
            encoding="utf-8",
        )

        subprocess.run(
            [
                sys.executable,
                str(renderer),
                "--release-json",
                str(release_json),
                "--canonical-url",
                "https://snaptray.cc/releases/1.2.3/",
                "--output",
                str(output_path),
            ],
            check=True,
        )

        html = output_path.read_text(encoding="utf-8")
        expected_fragments = [
            "<title>SnapTray 1.2.3</title>",
            'rel="canonical" href="https://snaptray.cc/releases/1.2.3/"',
            "<h1>SnapTray 1.2.3</h1>",
            "March 19, 2026",
            "<h3>What&#x27;s Changed</h3>",
            "<strong>auto update</strong>",
            '<a href="https://example.com/docs" target="_blank" rel="noopener">docs</a>',
            "<code>SnapTray</code>",
            "View on GitHub",
        ]

        for fragment in expected_fragments:
            if fragment not in html:
                raise AssertionError(f"expected fragment not found: {fragment}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
