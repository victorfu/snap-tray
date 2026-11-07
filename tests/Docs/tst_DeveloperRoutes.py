"""Every labeled developer sidebar link must have a real localized target."""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(sys.argv.pop(1)).resolve()


class DeveloperRoutes(unittest.TestCase):
    def test_localized_sidebar_targets(self):
        routes = {}
        key = None
        for line in (ROOT / "docs/_data/routes.yml").read_text(encoding="utf-8").splitlines():
            if re.fullmatch(r"[a-z_]+:", line):
                key = line[:-1]
                routes[key] = {}
            match = re.fullmatch(r"  ([a-z-]+): (/.+)", line)
            if match and key:
                routes[key][match[1]] = match[2]
        permalinks = set()
        for page in (ROOT / "docs").rglob("*.md"):
            if "_site" in page.parts or "vendor" in page.parts:
                continue
            match = re.search(r"^permalink: (.+)$", page.read_text(encoding="utf-8"), re.MULTILINE)
            if match:
                permalinks.add(match[1])
        key = None
        checked = 0
        for line in (ROOT / "docs/_data/developer_nav.yml").read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"    - route_key: ([a-z_]+)", line)
            if match:
                key = match[1]
            match = re.fullmatch(r"        ([a-z-]+): .+", line)
            if match and key:
                with self.subTest(route=key, language=match[1]):
                    self.assertIn(match[1], routes[key])
                    self.assertIn(routes[key][match[1]], permalinks)
                checked += 1
        self.assertGreater(checked, 0)


if __name__ == "__main__":
    unittest.main()
