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

## Command groups

### Local capture commands

Run without main instance for direct capture operations.

### IPC commands

Send actions to running SnapTray instance (GUI, canvas, pin, record).

### Config commands

Read or update settings from scripts.

## Example commands

```bash
snaptray --help
snaptray region --copy
snaptray screen --save
snaptray gui --show
snaptray config --get files.screenshotPath
```

## Return codes

Use exit codes in scripts/CI automation to detect failures and retries.
