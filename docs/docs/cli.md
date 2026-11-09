---
last_modified_at: 2026-03-24
layout: docs
title: CLI
seo_title: "SnapTray CLI: Automate Screenshots, Pinning, and Settings"
description: The official automation interface for scripting capture, pin, and settings workflows.
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

### Linux beta

Open Settings > General and use the CLI install action to create a `~/.local/bin/snaptray` wrapper for the AppImage. Make sure `~/.local/bin` is in your `PATH`, then open a new terminal after install or uninstall.

### Packaged Windows builds

Some installers may already expose `snaptray` through `PATH` or App Execution Alias, but the in-app install and remove flow is the canonical behavior described by the current application code.

## Command matrix

| Command | Description | Requires main SnapTray instance |
|---|---|---|
| `full` | Capture the full screen under the cursor, or a selected screen with `-n` | No |
| `screen` | Capture a specific screen, or list screens with `--list` | No |
| `region` | Capture `-r x,y,width,height` from a selected screen | No |
| `gui` | Open the region capture GUI; with script options, wait for the selection and deliver it | Yes |
| `canvas` | Toggle Screen Canvas mode | Yes |
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
snaptray gui -s -c                    # Select, copy on mouse release (no toolbar)
snaptray gui -c -o shot.png           # Annotate, then Enter copies and saves
snaptray gui --raw > shot.png         # Write the finished capture as PNG to stdout
snaptray gui --pin                    # Pin the finished capture
snaptray gui -s -g                    # Print the selection, e.g. 800x600+100+200
snaptray gui -s -g --json             # {"x":100,"y":200,"width":800,"height":600,"screen":0}
snaptray gui --last-region -c         # Start from the last captured region
snaptray gui -n 1 -r 100,100,400,300 -o region.png  # Start from a region on screen 1
snaptray canvas                       # Toggle Screen Canvas
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
- On Linux X11, clipboard captures remain available after the CLI exits without requiring the tray app or a clipboard manager. A background clipboard owner exits when another copy replaces the image.
- `--output` takes priority over `--path`. If neither is provided, SnapTray generates a filename in the configured screenshot directory.
- `screen` supports both `snaptray screen 1` and `snaptray screen -n 1`.
- `region` requires `-r/--region`, uses logical pixels relative to the selected screen, and the rectangle must fit inside that screen.
- `pin` requires exactly one of `--file` or `--clipboard`. `--file` must be a readable image. Custom placement is applied only when both `-x` and `-y` are provided; otherwise the pin is centered.
- `gui` without destination options keeps the original behavior: it opens the region selector and returns right away; the toolbar decides what happens.
- `gui` destinations are `-c/--clipboard`, `-o/--output`, `-p/--path`, `--raw` and `--pin`. Several can be combined (for example `-c -o shot.png`). With a destination, the toolbar's Pin button and Enter run them, and you can still annotate first. Saving follows the same rules as `full` (`--output` wins over `--path`).
- `gui` with a destination or `-g/--print-geometry` waits until you finish or cancel, with no time limit. Esc returns `5` and writes nothing to stdout. Closing the selector through another toolbar action (Copy, Save, and so on) also returns `5`. If SnapTray quits while the CLI waits, the CLI returns `1`.
- `-s/--accept-on-select` finishes on mouse release with no toolbar, so there is no annotation. It needs a destination or `--print-geometry`.
- `-g/--print-geometry` prints `WIDTHxHEIGHT+X+Y` in global logical pixels, or JSON with `--json`. It cannot be combined with `--raw`, because both write to stdout. To reuse the output with `region -r`, subtract the origin of the reported screen and pass that screen with `-n`. On a single screen at the origin, the numbers are the same.
- `gui -r/--region x,y,width,height` (optionally with `-n`) starts from that region, using the same coordinate rules as the `region` command. `--last-region` starts from the region of the last completed capture. If that region no longer fits the current screen layout, the capture starts without a selection and a warning is printed to stderr. You can adjust either initial region before finishing.
- Only one capture runs at a time. If a capture is already open, a waiting `gui` returns `1` and explains why.
- `config --set` accepts a single positional value. `config --reset` clears the entire settings store.

## Return codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | General error |
| `2` | Invalid arguments |
| `3` | File error |
| `4` | Instance error (main app not running) |
| `5` | Cancelled (`gui` selection cancelled with Esc) |

## Related docs

- [Getting Started](/docs/getting-started/)
- [Troubleshooting](/docs/troubleshooting/)
