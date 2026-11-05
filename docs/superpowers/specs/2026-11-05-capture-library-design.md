# Capture Library — Design

Issue: [#226](https://github.com/victorfu/snap-tray/issues/226). Tracking: #221. Follows #225.

## Problem

Screenshots and recordings are browsed in two unrelated places:

- **History** (`HistoryWindow.qml`) is a full window with a sidebar, smart folders, sort, grid/list views and one command bar acting on the selected screenshot.
- **Recording Library** (`RecordingLibraryDialog.qml`) is a small dialog with a fixed list. Every row carries its own Open, Show in Folder and Delete buttons.

They also describe what they hold in different ways:

- **Screenshots:** "Delete" removes SnapTray's history copy (the entry folder) at once, without asking. Files the user saved elsewhere stay, but nothing says so.
- **Recordings:** "Delete..." removes an *editable project*, which is the full, unmasked recording plus its edits. The exported file stays. The dialog asks first and says the exported file stays.

## Decisions

| Question | Decision |
| --- | --- |
| Entry | One window, **Library** (the History window, renamed). The tray item and hotkey "History" become "Library"; the stored hotkey key is unchanged. The recording group's "Recording Library..." opens the same window on **Recordings**. The Recording Library dialog is removed. |
| Navigation | A sidebar **Library** group: **All Captures**, **Screenshots** and **Recordings**, each with a count. The existing smart folders (Last 7 Days, Large Files, Replayable) stay under **Smart Folders** and filter screenshots. |
| Storage | Both storage models stay as they are: `HistoryStore` for screenshots and `RecordingProjects` for projects. A new read-only `LibraryModel` maps both into one list. |
| Actions | One command bar and one context menu act on the selection, as History does today. There are no buttons on each row. The actions depend on the selected item's kind (below). |
| Delete | Both kinds ask first, in a confirmation strip above the results. The strip says what goes and what stays. |
| Out of scope | Search, favourites, rename and batch actions (issue: separate candidates), and any change to either storage format. |

## Data-source mapping

| Library field | Screenshot (`HistoryModel` row) | Recording (`RecordingProject`) |
| --- | --- | --- |
| kind | `screenshot` | `recording` |
| key | history entry id | project folder |
| title | `displayTitle` | `title()`: the saved file's name, or the date |
| kind label | "Screenshot" | "Editable project" |
| thumbnail | `thumbnailUrl` | `thumbnail.png` |
| time (sort, "All") | `capturedAt` | `created` (when it was recorded; re-exporting does not move it) |
| size (sort) | `fileSizeBytes` | `sizeBytes()` (the whole project folder) |
| meta line | relative date · size · resolution | relative date · duration · size |
| detail lines | the existing tooltip lines (app, window) | the saved file's name, or "Not saved yet", or "Saved file was moved or deleted" |
| busy | — | open in the recording preview |

"All Captures" merges both lists in the window's sort order (newest, oldest or largest first). The smart folders and their counts apply to screenshots only. Recordings are sorted by `LibraryModel` in the same order.

## Actions by kind

| Screenshot | Recording |
| --- | --- |
| **Edit** (Return, double-click) | **Open in Editor** (Return, double-click) |
| Pin | Show Saved File (only when that file exists) |
| Copy | Show Project Folder |
| Save As... | |
| **Delete...** | **Delete Project...** |

When the selected project is open in the recording preview, Open in Editor and Delete Project are disabled. The detail line then reads "Open in the recording preview".

### Delete confirmation

Delete key, the command bar or the context menu shows a strip above the results. It holds the message, a primary **Delete** button and **Cancel**. Esc or a change of selection cancels it.

- Screenshot: "Delete this screenshot from History? Files you saved from it stay."
- Recording: "Delete this editable project? Its full recording and edits are removed. The file you saved from it stays."

## Recordings: privacy, retention, setting

While the selection or the section includes recordings, a note sits above the results:

- "Projects keep the full recording, including anything you blurred or cropped out." Taken unchanged from the dialog.
- When `projectCleanupDays()` is greater than 0: "Projects not changed for %n day(s) are removed automatically."
- When keeping is off: "Keeping new projects is off." The Recordings empty state says how to turn it on, as the dialog did.

Crash recovery ("Recover Recordings...") and cleanup at start-up are unchanged.

## Layout

### Wide (window width ≥ 820)

The sidebar keeps its current width (23 %, 180–244 px). The content column holds:
1. the command bar;
2. the header (the section name, the sort and the grid/list toggle);
3. the note or the confirmation strip;
4. the results.

### Narrow (< 820)

The sidebar is hidden. A row of three section buttons (All, Screenshots, Recordings) sits above the command bar. Smart folders are reached through the sort row's **Filter** menu (Screenshots only). The window's minimum width drops from 960 to 640, so this layout is reachable.

### Selection

- The selected card or row uses the existing selected tokens.
- The command bar shows only the selected kind's actions and names the selection: "Screenshot selected" or "Editable project selected".
- With no selection, every action is disabled.

### Accessibility

- Cards and rows are `Accessible.ListItem`, named by title and kind label.
- Command-bar buttons are `Accessible.Button` with their text as the name.
- Section buttons expose `checked`.
- The confirmation strip's Delete button takes focus when it opens, and focus returns to the results when it closes.

## Testing

- `LibraryModel` unit tests: the source filter, merge order for each sort, roles for both kinds, the busy project and refresh on either source changing.
- A QML test on the Library window:
  - sections and counts;
  - each kind's actions;
  - delete confirmation for both kinds (cancel, then confirm);
  - the privacy note;
  - the narrow layout;
  - the open-in-preview state.
- The Recording Library tests in `tst_RecordingSetup` move to the window. The library dialog no longer exists.
- Gated evidence screenshots:
  - wide in light, dark and German;
  - narrow;
  - Recordings with the note;
  - the delete strip.
- `./scripts/run-tests.sh` on macOS. Windows and Ubuntu manual checks go to #238 and are not marked as passed.
