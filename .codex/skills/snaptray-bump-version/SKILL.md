---
name: snaptray-bump-version
description: Bump the SnapTray app version in this repository when the user asks to bump version, release a new patch version, or update the app version number. Use only for SnapTray version bumps. The source of truth is `project(SnapTray VERSION ...)` in `CMakeLists.txt`. Every release bump must also update `CHANGELOG.md` with curated user-facing highlights derived from commit messages between the previous release tag and the new version, then create the matching local `v<version>` tag without pushing it. Prefer patch bumps unless the user gives an exact target version.
---

# SnapTray bump version

Use this skill when the user asks to bump SnapTray's version.

## Rules

1. Treat `project(SnapTray VERSION ...)` in `CMakeLists.txt` as the source of truth.
2. By default, bump only the patch version.
3. If the user gives an explicit target version, use that exact version instead of auto-incrementing.
4. Determine the previous release tag before editing anything. Prefer the exact tag for the pre-bump version, such as `v1.0.41`. If it does not exist, fall back to the nearest lower semantic-version tag and note the fallback in the final report.
5. If `CHANGELOG.md` is missing, create it before release prep with the standard SnapTray scaffold:
   - `# Changelog`
   - short explanatory intro
   - `## [Unreleased]`
   - an empty or placeholder bullet
6. `CHANGELOG.md` is required for every releasable bump. The new version section must be curated from commit subjects in the range `<previous_tag>..HEAD`, gathered with `git log --no-merges --format=%s`.
7. Do not dump raw commit subjects into `CHANGELOG.md`. Collapse related commits into concise, user-facing bullets grouped under `Added`, `Improved`, and `Fixed` when applicable. Ignore docs/test/chore noise unless it materially affects users, installers, or release behavior.
8. Use the local current date for version headings in `CHANGELOG.md`, in the format `## [1.0.42] - YYYY-MM-DD`.
9. Create the local tag `v<version>` after the release-prep commit is created and verified. Never push the tag.
10. If `refs/tags/v<version>` already exists locally, stop and inspect instead of moving or recreating it.
11. Do not add Xcode or `MARKETING_VERSION` guidance. SnapTray release versioning is CMake-driven.
12. Do not edit generated version outputs or duplicate version strings elsewhere unless the task explicitly requires it.
13. Before committing, run `git diff --check`.
14. Verify the release notes with `python3 scripts/extract_changelog_entry.py <version>`.
15. Verify the local tag with `git rev-parse -q --verify refs/tags/v<version>`.
16. Follow the repo's Conventional Commit + Lore trailer protocol for the release-prep commit. Do not use a bare version-number commit message.

## Workflow

1. Read the current version from `CMakeLists.txt`.
2. Decide the next version:
   - explicit version from the user wins
   - otherwise increment the patch number
3. Determine the previous release tag for the pre-bump version and gather commit subjects for the release window:
   - prefer `v<current_version>` when it exists
   - gather source material with `git log --no-merges --format=%s <previous_tag>..HEAD`
4. Update `project(SnapTray VERSION ...)` in `CMakeLists.txt`.
5. Ensure `CHANGELOG.md` exists:
   - if missing, create the standard scaffold with `# Changelog`, the short intro text, and a top-level `## [Unreleased]` section
6. Update `CHANGELOG.md` for the bumped version:
   - create or replace `## [<version>] - <today>` with curated release notes derived from the commit subjects in the release window
   - prefer shipped user-visible outcomes over internal implementation detail
   - merge repetitive fixes or perf commits into a few meaningful bullets instead of preserving commit granularity
   - keep `## [Unreleased]` only as a staging area for future work; do not use placeholder bullets in the releasable version section
7. Keep the change minimal. Do not edit release docs, generated outputs, or unrelated files.
8. Verify with `git diff --check`.
9. Run `python3 scripts/extract_changelog_entry.py <version>`.
10. Stage and create the release-prep commit:
   - `git add CMakeLists.txt CHANGELOG.md`
   - commit with a Conventional Commit subject plus Lore trailers, for example:

```text
chore(release): prepare v1.0.42

Constraint: SnapTray release notes are sourced from CHANGELOG.md
Constraint: Release notes must reflect the commit window since the previous tag
Rejected: Copy raw commit subjects into CHANGELOG.md | too noisy for release consumers
Confidence: high
Scope-risk: narrow
Directive: Keep CHANGELOG.md, CMakeLists.txt, and the local release tag aligned for every version bump
Tested: git diff --check; python3 scripts/extract_changelog_entry.py 1.0.42
Not-tested: Tag push and GitHub Actions release run
```

11. Create the local tag with `git tag v<version>`.
12. Verify the tag with `git rev-parse -q --verify refs/tags/v<version>`.
13. Stop after the local tag. Do not push the tag or trigger publishing unless the user explicitly asks.

## Notes

- SnapTray's release path is `CHANGELOG.md -> GitHub Release body -> website release page`.
- SnapTray packaging scripts already read the version from `CMakeLists.txt`, so versioning should stay single-source.
- If `CHANGELOG.md` does not exist yet, this skill should create the file instead of treating that as a blocker.
- A SnapTray version bump is release prep in this repo: the done state includes a curated changelog entry and a matching local tag.
- Even after the local tag exists, do not push the release tag automatically; leave tag push to manual execution.
- Use commit messages as source material, not as final prose. The changelog should read like release notes, not `git log`.
- Keep the change minimal. A version bump task is not a cleanup or release-refactor task.
