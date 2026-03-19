#!/usr/bin/env python3
"""Render a lightweight release notes HTML page from a GitHub release JSON payload."""

from __future__ import annotations

import argparse
import datetime as dt
import html
import json
import re
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-json", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--canonical-url", default="")
    return parser.parse_args()


def escape(text: str) -> str:
    return html.escape(text, quote=True)


def format_inline(text: str) -> str:
    safe = escape(text)
    safe = re.sub(r"`([^`]+)`", r"<code>\1</code>", safe)
    safe = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", safe)
    safe = re.sub(r"\*([^*]+)\*", r"<em>\1</em>", safe)
    safe = re.sub(
        r"\[([^\]]+)\]\((https?://[^\s)]+)\)",
        r'<a href="\2" target="_blank" rel="noopener">\1</a>',
        safe,
    )
    return safe


def render_markdown(body: str) -> str:
    if not body.strip():
        return "<p>No release notes provided.</p>"

    lines = body.splitlines()
    html_parts: list[str] = []
    in_list = False

    def close_list() -> None:
        nonlocal in_list
        if in_list:
            html_parts.append("</ul>")
            in_list = False

    for raw_line in lines:
        line = raw_line.strip()

        if not line:
            close_list()
            continue

        if line.startswith("### "):
            close_list()
            html_parts.append(f"<h4>{format_inline(line[4:])}</h4>")
            continue

        if line.startswith("## "):
            close_list()
            html_parts.append(f"<h3>{format_inline(line[3:])}</h3>")
            continue

        if line.startswith("# "):
            close_list()
            html_parts.append(f"<h2>{format_inline(line[2:])}</h2>")
            continue

        if line.startswith("- ") or line.startswith("* "):
            if not in_list:
                html_parts.append("<ul>")
                in_list = True
            html_parts.append(f"<li>{format_inline(line[2:])}</li>")
            continue

        close_list()
        html_parts.append(f"<p>{format_inline(line)}</p>")

    close_list()
    return "\n".join(html_parts)


def format_published_at(value: str) -> str:
    if not value:
        return ""

    parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    return f"{parsed.strftime('%B')} {parsed.day}, {parsed.year}"


def render_page(release: dict[str, object], canonical_url: str) -> str:
    version = str(release.get("tag_name", "")).removeprefix("v") or "Unknown"
    title = str(release.get("name") or f"SnapTray {version}")
    published_at = format_published_at(str(release.get("published_at", "")))
    body = str(release.get("body") or "")
    github_url = str(release.get("html_url") or "")
    content = render_markdown(body)

    canonical = (
        f'  <link rel="canonical" href="{escape(canonical_url)}">\n'
        if canonical_url
        else ""
    )
    release_link = (
        f'<a class="meta-link" href="{escape(github_url)}" target="_blank" rel="noopener">'
        "View on GitHub"
        "</a>"
        if github_url
        else ""
    )
    meta_line = " · ".join(part for part in [published_at, release_link] if part)
    meta_html = f"<p class=\"meta\">{meta_line}</p>" if meta_line else ""

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{escape(title)}</title>
  <meta name="description" content="Release notes for SnapTray {escape(version)}.">
{canonical}  <style>
    :root {{
      color-scheme: light;
      --bg: #f5f7fb;
      --card: #ffffff;
      --line: #d9e0ea;
      --text: #152033;
      --muted: #5d6b82;
      --accent: #2f66ff;
      --accent-soft: #edf3ff;
      --code-bg: #eef2f7;
      --shadow: 0 20px 40px rgba(20, 32, 51, 0.08);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      background:
        radial-gradient(circle at top right, rgba(47, 102, 255, 0.14), transparent 32%),
        linear-gradient(180deg, #f9fbff 0%, var(--bg) 100%);
      color: var(--text);
    }}
    main {{
      max-width: 760px;
      margin: 0 auto;
      padding: 32px 20px 48px;
    }}
    .shell {{
      background: var(--card);
      border: 1px solid rgba(217, 224, 234, 0.85);
      border-radius: 22px;
      padding: 28px 28px 20px;
      box-shadow: var(--shadow);
    }}
    .eyebrow {{
      display: inline-flex;
      align-items: center;
      gap: 8px;
      margin: 0 0 14px;
      padding: 8px 12px;
      border-radius: 999px;
      background: var(--accent-soft);
      color: var(--accent);
      font-size: 13px;
      font-weight: 600;
      letter-spacing: 0.04em;
      text-transform: uppercase;
    }}
    h1 {{
      margin: 0;
      font-size: 30px;
      line-height: 1.1;
    }}
    .meta {{
      margin: 12px 0 0;
      color: var(--muted);
      font-size: 14px;
    }}
    .meta-link {{
      color: var(--accent);
      text-decoration: none;
    }}
    .meta-link:hover {{
      text-decoration: underline;
    }}
    .content {{
      margin-top: 26px;
      font-size: 16px;
      line-height: 1.65;
    }}
    .content h2,
    .content h3,
    .content h4 {{
      margin: 24px 0 10px;
      line-height: 1.25;
    }}
    .content p {{
      margin: 0 0 14px;
    }}
    .content ul {{
      margin: 0 0 18px;
      padding-left: 22px;
    }}
    .content li {{
      margin-bottom: 8px;
    }}
    .content a {{
      color: var(--accent);
      text-decoration: none;
    }}
    .content a:hover {{
      text-decoration: underline;
    }}
    .content code {{
      padding: 2px 6px;
      border-radius: 8px;
      background: var(--code-bg);
      font-size: 0.92em;
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    }}
    @media (max-width: 560px) {{
      main {{
        padding: 16px 12px 24px;
      }}
      .shell {{
        border-radius: 16px;
        padding: 18px 16px 12px;
      }}
      h1 {{
        font-size: 24px;
      }}
      .content {{
        font-size: 15px;
      }}
    }}
  </style>
</head>
<body>
  <main>
    <section class="shell">
      <p class="eyebrow">SnapTray Release Notes</p>
      <h1>{escape(title)}</h1>
      {meta_html}
      <div class="content">
{content}
      </div>
    </section>
  </main>
</body>
</html>
"""


def main() -> int:
    args = parse_args()
    release_path = Path(args.release_json)
    release = json.loads(release_path.read_text(encoding="utf-8"))
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_page(release, args.canonical_url), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
