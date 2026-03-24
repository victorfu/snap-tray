---
name: snaptray-bump-version
description: Bump the SnapTray app version in this repository when the user asks to bump version, release a new patch version, or update the app version number. Use only for SnapTray version bumps. The source of truth is `project(SnapTray VERSION ...)` in `CMakeLists.txt`. Prefer patch bumps unless the user gives an exact target version. If committing the version bump, use only the version number as the commit message, for example `1.0.38`.
---

# SnapTray bump version

Use this skill when the user asks to bump SnapTray's version.

## Rules

1. Treat `project(SnapTray VERSION ...)` in `CMakeLists.txt` as the source of truth.
2. By default, bump only the patch version.
3. If the user gives an explicit target version, use that exact version instead of auto-incrementing.
4. Do not edit generated version outputs or duplicate version strings elsewhere unless the task explicitly requires it.
5. Before committing, run `git diff --check`.
6. If the user asks to commit the bump, the commit message must be the version number only, such as `1.0.38`.

## Workflow

1. Read the current version from `CMakeLists.txt`.
2. Decide the next version:
   - explicit version from the user wins
   - otherwise increment the patch number
3. Update only `project(SnapTray VERSION ...)` in `CMakeLists.txt`.
4. Verify with `git diff --check`.
5. If the user asked for a commit:
   - `git add CMakeLists.txt`
   - `git commit -m "<version>"`

## Notes

- SnapTray packaging scripts already read the version from `CMakeLists.txt`, so a normal version bump should stay single-source.
- Keep the change minimal. A version bump task is not a cleanup or release-refactor task.
