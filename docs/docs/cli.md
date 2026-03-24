---
layout: docs
title: CLI
description: The official automation interface for scripting capture, recording, pin, and settings workflows.
permalink: /docs/cli/
lang: en
route_key: docs_cli
doc_group: advanced
doc_order: 1
---

The CLI is SnapTray's official automation interface. Use it to script local capture workflows, control a running app through IPC, and manage settings from shells or CI jobs.

## Install CLI helper

### macOS

Open Settings > General and use the CLI install action to create `/usr/local/bin/snaptray`. This requires administrator privileges.

### Windows

Open Settings > General and use the CLI install action to add SnapTray's executable directory to the current user's `PATH`. Open a new terminal after install or uninstall.

### Packaged Windows builds

Some installers may already expose `snaptray` through `PATH` or App Execution Alias, but the in-app install and remove flow is the canonical behavior described by the current application code.

## Command matrix

| Command | Description | Requires main SnapTray instance |
|---|---|---|
| `full` | Capture the full screen under the cursor, or a selected screen with `-n` | No |
| `screen` | Capture a specific screen, or list screens with `--list` | No |
| `region` | Capture `-r x,y,width,height` from a selected screen | No |
| `gui` | Open the region capture GUI | Yes |
| `canvas` | Toggle Screen Canvas mode | Yes |
| `record` | Start, stop, or toggle recording | Yes |
| `pin` | Pin an image file or clipboard image | Yes |
| `config` | List, get, set, or reset settings; no options opens Settings | Partial |

## Example commands

```bash
# Help and version
snaptray --help
snaptray --version
snaptray full --help

# Local capture commands
snaptray full                         # Capture the screen under the cursor
snaptray full -c                      # Copy a full screen capture to the clipboard
snaptray full -d 1000 -o shot.png     # Delay 1 second, then save
snaptray full -n 1 -o screen1.png     # Capture screen 1 to file
snaptray full --raw > shot.png        # Write PNG bytes to stdout
snaptray screen --list                # List available screens
snaptray screen 0 -c                  # Capture screen 0 (positional syntax)
snaptray screen -n 1 -o screen1.png   # Capture screen 1 (option syntax)
snaptray screen 1 -o screen1.png      # Capture screen 1 to file
snaptray region -r 0,0,800,600 -c     # Capture a region from screen 0 to clipboard
snaptray region -n 1 -r 100,100,400,300 -o region.png
snaptray region -r 100,100,400,300 -o region.png

# IPC commands
snaptray gui                          # Open the region selector
snaptray gui -d 2000                  # Open after 2 seconds
snaptray canvas                       # Toggle Screen Canvas
snaptray record start                 # Start recording
snaptray record stop                  # Stop recording
snaptray record                       # Toggle recording
snaptray record start -n 1            # Start full-screen recording on screen 1
snaptray pin -f image.png             # Pin an image file
snaptray pin -c --center              # Pin clipboard image centered
snaptray pin -f image.png -x 200 -y 120
snaptray config                       # Open Settings dialog

# Local config commands
snaptray config --list
snaptray config --get hotkey
snaptray config --set files/filenamePrefix SnapTray
snaptray config --reset
```

## Behavior notes

- Capture commands (`full`, `screen`, `region`) save PNG by default.
- `--clipboard` copies instead of saving. `--raw` writes PNG bytes to stdout.
- `--output` takes priority over `--path`. If neither is provided, SnapTray generates a filename in the configured screenshot directory.
- `screen` supports both `snaptray screen 1` and `snaptray screen -n 1`.
- `region` requires `-r/--region`, uses logical pixels relative to the selected screen, and the rectangle must fit inside that screen.
- `record` accepts `start`, `stop`, or `toggle`. No action means `toggle`. In the current implementation, `-n/--screen` is consumed only by `record start`.
- `pin` requires exactly one of `--file` or `--clipboard`. `--file` must be a readable image. Custom placement is applied only when both `-x` and `-y` are provided; otherwise the pin is centered.
- `config --set` accepts a single positional value. `config --reset` clears the entire settings store.

## Return codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | General error |
| `2` | Invalid arguments |
| `3` | File error |
| `4` | Instance error (main app not running) |
| `5` | Recording error (`CLIResult::Code` defines it, but current CLI flows do not emit it) |

## Related docs

- [Getting Started](/docs/getting-started/)
- [Troubleshooting](/docs/troubleshooting/)
- [MCP (Debug Builds)](/developer/mcp-debug/) for debug-only localhost automation
