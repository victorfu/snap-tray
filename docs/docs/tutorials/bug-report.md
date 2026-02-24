---
layout: docs
title: Bug Report Workflow
description: Build a reproducible bug report with clear visuals, steps, and optional recording.
permalink: /docs/tutorials/bug-report/
lang: en
route_key: docs_tutorial_bug_report
doc_group: tutorials
doc_order: 3
---

## Goal

Produce a bug report that engineers can reproduce on the first try.

## Output package

- 1 annotated screenshot
- 1 short reproduction step list
- Optional 10-30 second recording

## Workflow

### 1. Reproduce once and freeze the state

- Reproduce the issue.
- Stop at the exact broken state.
- If you need to compare with expected behavior, capture and pin both states.

### 2. Capture the evidence

1. Press `F2` to capture.
2. Keep surrounding context (toolbar, tab name, status area).
3. Avoid over-cropping; include clues needed for reproduction.

### 3. Annotate for diagnosis

1. Use `Step Badge` to mark sequence (`1`, `2`, `3`).
2. Use `Arrow` to point to the failure location.
3. Use `Text` for concise facts: error code, unexpected value, or timestamp.
4. Use `Mosaic` for private tokens or user data.

### 4. Export and name clearly

Save with a structured name such as:

`bug-login-timeout-win10-2026-02-24.png`

### 5. Optional: record short evidence video

If the issue depends on timing or animation:

1. Press `R` from region capture (or start from tray recording).
2. Record 10-30 seconds.
3. Stop and export as MP4.

## Bug report template

```markdown
Title: [Platform] Short problem summary

Environment:
- App version:
- OS:

Steps to reproduce:
1. ...
2. ...
3. ...

Expected:
Actual:

Attachments:
- screenshot.png
- recording.mp4 (optional)
```

## Quality checklist

- Reproduction steps are deterministic.
- Screenshot includes both problem and context.
- Annotation explains sequence and impact.
- Private data is masked.

## Next tutorial

Use [Live Reference with Pin](/docs/tutorials/live-reference/) to keep specs or bug screenshots on screen while fixing.
