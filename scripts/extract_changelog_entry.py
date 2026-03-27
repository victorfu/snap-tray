#!/usr/bin/env python3
"""Extract a single release section from CHANGELOG.md."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


HEADING_PATTERN = re.compile(r"^## \[(?P<version>[^\]]+)\](?: - (?P<date>\d{4}-\d{2}-\d{2}))?\s*$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="Version to extract, with or without a leading 'v'")
    parser.add_argument("--input", default="CHANGELOG.md", help="Path to the changelog file")
    parser.add_argument("--output", default="-", help="Output path or '-' for stdout")
    return parser.parse_args()


def normalize_version(value: str) -> str:
    return value.removeprefix("v").strip()


def extract_section(text: str, version: str) -> str:
    normalized_target = normalize_version(version)
    lines = text.splitlines()
    headings: list[tuple[int, str]] = []

    for index, line in enumerate(lines):
        match = HEADING_PATTERN.match(line.strip())
        if match:
            headings.append((index, match.group("version")))

    if not headings:
        raise ValueError("No changelog version headings were found.")

    for offset, (start_index, heading_version) in enumerate(headings):
        if normalize_version(heading_version) != normalized_target:
            continue

        end_index = headings[offset + 1][0] if offset + 1 < len(headings) else len(lines)
        section_lines = lines[start_index + 1:end_index]
        section = "\n".join(section_lines).strip()
        if not section:
            raise ValueError(f"Changelog entry for version {normalized_target} is empty.")
        return section + "\n"

    raise ValueError(f"No changelog entry found for version {normalized_target}.")


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)

    try:
        changelog_text = input_path.read_text(encoding="utf-8")
        section = extract_section(changelog_text, args.version)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.output == "-":
        sys.stdout.write(section)
        return 0

    output_path = Path(args.output)
    output_path.write_text(section, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
