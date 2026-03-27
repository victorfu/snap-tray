---
name: snaptray-bump-version
description: Bump the SnapTray app version in this repository when the user asks to bump version, release a new patch version, or update the app version number. Use only for SnapTray version bumps. The source of truth is `project(SnapTray VERSION ...)` in `CMakeLists.txt`, and release prep must create or update `CHANGELOG.md` so the bumped version has a matching release-notes section. Prefer patch bumps unless the user gives an exact target version.
---

# SnapTray bump version

Use this skill when the user asks to bump SnapTray's version.

## Rules

1. Treat `project(SnapTray VERSION ...)` in `CMakeLists.txt` as the source of truth.
2. By default, bump only the patch version.
3. If the user gives an explicit target version, use that exact version instead of auto-incrementing.
4. If `CHANGELOG.md` is missing, create it before release prep with the standard SnapTray scaffold:
   - `# Changelog`
   - short explanatory intro
   - `## [Unreleased]`
   - an empty or placeholder bullet
5. Keep `CHANGELOG.md` aligned with the bumped version when the task is release prep or version preparation. SnapTray release notes come from the matching changelog section.
6. Use the local current date for version headings in `CHANGELOG.md`, in the format `## [1.0.42] - YYYY-MM-DD`.
7. Do not add Xcode or `MARKETING_VERSION` guidance. SnapTray release versioning is CMake-driven.
8. Do not edit generated version outputs or duplicate version strings elsewhere unless the task explicitly requires it.
9. Before committing, run `git diff --check`.
10. If `CHANGELOG.md` changed, verify it with `python3 scripts/extract_changelog_entry.py <version>`.
11. If the user asks to commit the bump, follow the repo's Conventional Commit + Lore trailer protocol. Do not use a bare version-number commit message.

## Workflow

1. Read the current version from `CMakeLists.txt`.
2. Decide the next version:
   - explicit version from the user wins
   - otherwise increment the patch number
3. Update `project(SnapTray VERSION ...)` in `CMakeLists.txt`.
4. Ensure `CHANGELOG.md` exists:
   - if missing, create the standard scaffold with `# Changelog`, the short intro text, and a top-level `## [Unreleased]` section
5. Update `CHANGELOG.md` for the bumped version when preparing a releasable version:
   - if `## [Unreleased]` contains real notes, promote that content into `## [<version>] - <today>` and recreate a fresh `## [Unreleased]` placeholder at the top
   - otherwise create a new `## [<version>] - <today>` section with a minimal placeholder note so the release extractor does not fail
6. Keep the change minimal. Do not tag, push, or edit release docs unless the user explicitly asked for release preparation beyond the version bump.
7. Verify with `git diff --check`.
8. If `CHANGELOG.md` changed, run `python3 scripts/extract_changelog_entry.py <version>`.
9. If the user asked for a commit:
   - `git add CMakeLists.txt CHANGELOG.md`
   - commit with a Conventional Commit subject plus Lore trailers, for example:

```text
chore(release): prepare v1.0.42

Constraint: SnapTray release notes are sourced from CHANGELOG.md
Confidence: high
Scope-risk: narrow
Directive: Keep CHANGELOG.md and CMakeLists.txt aligned before tagging v1.0.42
Tested: git diff --check; python3 scripts/extract_changelog_entry.py 1.0.42
Not-tested: Full tag push and GitHub Actions release run
```

10. If the user explicitly asked to tag or publish the release, you may create the local tag with `git tag v<version>`, but leave pushing the tag to manual execution.

## Notes

- SnapTray's release path is `CHANGELOG.md -> GitHub Release body -> website release page`.
- SnapTray packaging scripts already read the version from `CMakeLists.txt`, so versioning should stay single-source.
- If `CHANGELOG.md` does not exist yet, this skill should create the file instead of treating that as a blocker.
- A bump-only request does not imply tagging.
- Even for release prep, do not push the release tag automatically; leave tag push to manual execution.
- Keep the change minimal. A version bump task is not a cleanup or release-refactor task.
