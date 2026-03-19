#!/usr/bin/env python3
"""Generate a minimal Sparkle-compatible appcast for SnapTray releases."""

from __future__ import annotations

import argparse
import email.utils
import mimetypes
from pathlib import Path
from xml.sax.saxutils import escape


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=["macos", "windows"], required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--download-url", required=True)
    parser.add_argument("--signature", required=True, help="EdDSA signature for the enclosure")
    parser.add_argument("--length", type=int, required=True, help="Download size in bytes")
    parser.add_argument("--output", required=True)
    parser.add_argument("--pub-date", default=email.utils.formatdate(usegmt=True))
    parser.add_argument("--title", default="SnapTray")
    parser.add_argument("--release-notes-url", default="")
    parser.add_argument("--minimum-system-version", default="")
    return parser.parse_args()


def enclosure_type(download_url: str) -> str:
    guessed, _ = mimetypes.guess_type(download_url)
    return guessed or "application/octet-stream"


def render_appcast(args: argparse.Namespace) -> str:
    minimum_system = ""
    if args.minimum_system_version:
        minimum_system = (
            "\n"
            f"      <sparkle:minimumSystemVersion>{escape(args.minimum_system_version)}</sparkle:minimumSystemVersion>"
        )

    release_notes = ""
    if args.release_notes_url:
        release_notes = (
            f"\n      <sparkle:releaseNotesLink>{escape(args.release_notes_url)}</sparkle:releaseNotesLink>"
        )

    return f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0"
     xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"
     xmlns:dc="http://purl.org/dc/elements/1.1/">
  <channel>
    <title>{escape(args.title)} {escape(args.platform)}</title>
    <link>{escape(args.download_url)}</link>
    <description>SnapTray update feed for {escape(args.platform)}</description>
    <language>en</language>
    <item>
      <title>Version {escape(args.version)}</title>
      <pubDate>{escape(args.pub_date)}</pubDate>
{minimum_system}{release_notes}
      <enclosure
          url="{escape(args.download_url)}"
          sparkle:version="{escape(args.version)}"
          sparkle:shortVersionString="{escape(args.version)}"
          sparkle:edSignature="{escape(args.signature)}"
          length="{args.length}"
          type="{escape(enclosure_type(args.download_url))}" />
    </item>
  </channel>
</rss>
"""


def main() -> int:
    args = parse_args()
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_appcast(args), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
