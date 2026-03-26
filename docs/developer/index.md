---
last_modified_at: 2026-03-24
layout: docs
title: Developer Docs
description: Canonical technical documentation for building, packaging, and extending SnapTray without overloading the GitHub README.
permalink: /developer/
lang: en
route_key: developer_home
nav_data: developer_nav
docs_copy_key: developer
---

This section is the canonical home for SnapTray's technical documentation. The root README is intentionally product-oriented; the material that used to live there now belongs here.

## What lives here

- [Build from Source](build-from-source.md): prerequisites, scripts, manual CMake flows, cache setup, and local build troubleshooting
- [Release & Packaging](release-packaging.md): DMG / NSIS / MSIX packaging, signing, notarization, Store submission, and icon asset workflow
- [Architecture Overview](architecture.md): repository structure, library boundaries, architecture patterns, platform-specific code, and development conventions
- [MCP (Debug Builds)](mcp-debug.md): the built-in localhost MCP server, tool contracts, and integration notes

## Audience split

- GitHub `README`: product landing page, install/download entry points, and high-level workflow overview
- `/docs/*`: end-user tutorials, feature references, troubleshooting, FAQ, and CLI usage
- `/developer/*`: contributor and agent-facing technical reference

## Source of truth policy

- Product marketing copy belongs on the README and website homepage.
- User workflow detail belongs in `/docs/*`.
- Build, packaging, architecture, and debug-only integration detail belongs in `/developer/*`.
- `AGENTS.md` stays concise and points back to these pages instead of duplicating long inventories that drift over time.

## Recommended reading order

1. [Build from Source](build-from-source.md)
2. [Architecture Overview](architecture.md)
3. [Release & Packaging](release-packaging.md)
4. [MCP (Debug Builds)](mcp-debug.md) if you need debug automation

## Related user docs

- [Getting Started](../docs/getting-started.md)
- [CLI](../docs/cli.md)
- [Troubleshooting](../docs/troubleshooting.md)
- [FAQ](../docs/faq.md)
