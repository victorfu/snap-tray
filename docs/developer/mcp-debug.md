---
last_modified_at: 2026-03-24
layout: docs
title: MCP (Debug Builds)
description: The built-in localhost MCP server is available only in debug builds and is intended for local automation and tooling workflows.
permalink: /developer/mcp-debug/
lang: en
route_key: developer_mcp_debug
nav_data: developer_nav
docs_copy_key: developer
---

## Availability

SnapTray's MCP server is a **debug-build-only** capability.

- `CMakeLists.txt` defines `SNAPTRAY_ENABLE_MCP` only for `Debug` builds
- The main app starts the MCP server only when that feature flag is compiled in
- Settings expose the MCP toggle only in debug builds

This is why MCP documentation lives under developer docs rather than in the product README.

## Transport and endpoint

- Transport: streamable HTTP
- Endpoint: `POST http://127.0.0.1:39200/mcp`
- `GET /mcp`: returns `405 Method Not Allowed`
- JSON-RPC methods: `initialize`, `notifications/initialized`, `tools/list`, `tools/call`, `ping`

## Security model

- The server binds to `127.0.0.1` only
- No bearer token is required for localhost in the current V1 implementation
- The intent is local developer tooling, not remote multi-user hosting

## Tool set

| Tool | Input | Output |
|---|---|---|
| `capture_screenshot` | `screen?`, `region? {x,y,width,height}`, `delay_ms?`, `output_path?` | `file_path`, `width`, `height`, `screen_index`, `created_at` |
| `pin_image` | `image_path`, `x?`, `y?`, `center?` | `accepted` |
| `share_upload` | `image_path`, `password?` | `url`, `expires_at`, `protected` |

When `capture_screenshot.output_path` is omitted, SnapTray writes to a temp folder.

## Runtime behavior

- The MCP server runs inside the main `snaptray` process
- No separate MCP binary is required
- Failure to bind the port surfaces a warning and a user-visible toast from the main application

## ChatGPT Desktop setup example

1. Open "Connect to a custom MCP"
2. Choose `Streamable HTTP`
3. Name the connection, for example `snaptray-local`
4. Use `http://127.0.0.1:39200/mcp`
5. Leave bearer token empty for localhost
6. Keep the debug build of SnapTray running

## Source references

- Transport and protocol: `src/mcp/`
- Startup integration: `src/MainApplication.cpp`
- Toggle state: `src/settings/MCPSettingsManager.cpp`

## Related docs

- [Build from Source](build-from-source.md)
- [CLI](../docs/cli.md)
