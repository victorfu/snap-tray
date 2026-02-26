---
layout: docs
title: CLI
description: Automate capture workflows with local and IPC command-line operations.
permalink: /docs/cli/
lang: en
route_key: docs_cli
doc_group: advanced
doc_order: 1
---

## Install CLI helper

Open Settings > General and use the CLI install option to add `snaptray` command support.

## Commands

| Command | Description | Requires main SnapTray instance |
|---|---|---|
| `full` | Capture full screen | No |
| `screen` | Capture specified screen | No |
| `region` | Capture specified region | No |
| `gui` | Open region capture GUI | Yes |
| `canvas` | Open/toggle Screen Canvas | Yes |
| `record` | `start` / `stop` / `toggle` recording | Yes |
| `pin` | Pin image from file or clipboard | Yes |
| `config` | List/get/set/reset config (`config` alone opens Settings) | Partial |

## Example commands

```bash
# Global
snaptray --help
snaptray --version

# Local capture commands
snaptray full -c
snaptray screen --list
snaptray screen 1 -o screen1.png
snaptray region -r 100,100,400,300 -o region.png

# IPC commands
snaptray gui -d 1500
snaptray canvas
snaptray record start -n 0
snaptray pin -f image.png --center

# Config commands
snaptray config --list
snaptray config --get hotkey
snaptray config --set hotkey F6
snaptray config --reset
```

## Return codes

Use exit codes in scripts/CI automation to detect failures and retries.
