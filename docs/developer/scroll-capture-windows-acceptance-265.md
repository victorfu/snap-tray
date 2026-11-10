# Windows scroll capture acceptance — issue #265

Tracking: [#265](https://github.com/victorfu/snap-tray/issues/265).
Run date: November 10, 2026 (Asia/Taipei). Baseline: `fe07efa0`.

## Usable Edge content export (schema 5)

The follow-up request explicitly expands the earlier diagnostic-only scope to
handle fixed regions and produce a real output. `--content-output file.png` now
uses the reusable `ScrollContentAssembler` core to select **one common content
rectangle for the entire capture**, validate every overlapping pixel, and append
the newly revealed strips. The initial frame prefix retains full width, so the
title above the main column is not truncated; the sidebar appears once and
excluded columns below it are transparent. No guessed pixels, seam blending,
rescaling, or per-frame crop changes are used.

Actual Edge issue-page runs on this Windows host, ten wheel-120 inputs each:

| System scale | Content alignment | Displacement / sample SD | Visual settle p95 | Common content rectangle (physical px) | PNG size |
| --- | --- | --- | --- | --- | --- |
| 100% | 10/10, exact overlap | 100 / 0 px | 258 ms | x4 y56, 700x587 | 1032x1643 |
| 150% | 10/10, exact overlap | 150 / 0 px | 302 ms | x4 y84, 1053x880 | 1553x2464 |

Both final PNGs were visually inspected: the issue title is complete, body text
and table remain continuous, and the sticky header/sidebar are not repeated in
the appended body. The output covers this ten-scroll segment, not the entire
issue. A first prototype cropped the title to the body-column width; it was
rejected during visual QA and replaced by the full-width initial prefix above.

Reviewable evidence checked into this PR:

- [100% PNG](evidence/issue265/edge100.png)
- [150% PNG](evidence/issue265/edge150.png)
- [Both final raw JSON records](evidence/issue265/edge-content.jsonl), including
  whole-frame diagnostics, API positions, per-band observations and separate
  content-export statistics. Local raw capture paths are relative to the repo;
  before/after captures remain under `build/issue265-content/{100,150}-final/`.

Whole-viewport statistics **still say 2/10**. The improvement is a real,
pixel-validated content export, not relabeling those conflicts. This does not
prove all four original #265 acceptance criteria or enable the product UI.
The capture target/position remain UIA-derived; the inner content rectangle is
image-derived and is explicitly reported as such. It is geometric, not a semantic
guarantee for arbitrary pages: dynamic/reflowing content may be rejected, and
exports should be inspected for intended coverage.

The core requires a majority of both viewport dimensions, unique row evidence,
positive displacement with overlap, exact continuity between repeats, and
bounded memory/output. Injection, settle, geometry and prior failures prevent
export. It has regression tests for sticky transitions, variable displacement,
pixel-exact output, no duplicated sidebar, discontinuities, blank/invalid input,
minority crops and allocation limits. Canonical build and targeted suites passed
3/3 (1.00 s); full-suite outcome is recorded with the PR. End-to-end export cost
is not included in the visual-settle p95 above.

After both final runs, Settings visibly showed **125%**, restored as requested.
Earlier sections below are retained as historical diagnosis/acceptance evidence.

## Edge sticky-sidebar diagnosis and retest (schema 4)

Published evidence: [issue follow-up](https://github.com/victorfu/snap-tray/issues/265#issuecomment-6761893473).

The 2/10 result is reproducible physical non-uniform motion, not another delayed
UIA-prior failure. Before/after PNG inspection confirms that the main content
continues scrolling while the sidebar reaches its sticky limit. On input 4
(zero-based index 3), unconstrained bands 5/6 measure only 8 px at 100% or 12 px
at 150%, while the five main-content bands measure 100/150 px. On inputs 5-10,
textured band 6 measures 0 px. All 20 new position-sync outcomes are
`within_search_window`; waiting for a fresher UIA offset cannot make the sidebar
and body share a displacement. Input 3 has insufficient detail in bands 6/7;
it remains unresolved, not a proven conflict. The last band also includes the
scrollbar/cursor highlight, so it is not used as sole proof of sidebar motion.

The fix is to the **classification and missing diagnostic evidence**, not a
claim that this page can now be stitched. `NonUniformMotion` /
`non_uniform_motion` distinguishes independently confirmed different shifts
from `Ambiguous` (competing uncertain estimates). Schema 4 exposes
`bandMatching.observedBands`, the unconstrained evidence actually used for
conflicts, in addition to prior-constrained `bands`. Previously a narrow prior
could leave only `not_found` in the visible band diagnostics despite a confirmed
conflict outside that range. No matching threshold, crop, prior, or acceptance
statistic was relaxed. Non-found `shiftPx` is diagnostic only.

| Same Edge issue page, delta 120 x10 | 100% | 150% |
| --- | --- | --- |
| Accepted alignment | 2/10 | 2/10 |
| Confirmed non-uniform motion | 7/10 | 7/10 |
| Unresolved | 1/10 | 1/10 |
| Dominant body displacement | 100 px, 10/10 | 150 px, 10/10 |
| Strict visual settle p95 | 285 ms | 280 ms |
| Injection / visual settle | 10/10 | 10/10 |

Canonical `scripts/build.bat` passed. Targeted ScrollCapture tests passed 2/2
(0.49 s), including uniform motion, a sidebar reaching its sticky limit, fixed
sidebar, equal competing panes, and narrow-prior conflict evidence. No full-suite
rerun is claimed for this diagnostic change. Source changes remain uncommitted.
Raw records and PNGs: `build/issue265-edge-fix/results.jsonl` and its `100`/`150`
directories; build/test logs: `build/issue265-edge-fix-{build,tests}.log`.

After the last run, Windows Settings visibly showed **125%**. Whole-region
alignment remains unsupported for these conflicting frame pairs. Making this
page stitchable requires a separate content-region/fixed-region treatment;
#265 explicitly defers fixed-region detection and stitching. The diagnostic fix
does not resolve that feature limitation, and capability remains off.

## Sidebars and remaining host matrix (schema 3)

The schema-2 dominant-displacement success is not whole-region acceptance.
`BandShiftEstimate::alignmentEstimate` now rejects confirmed conflicting bands
as `ambiguous` and unresolved bands as `not_found`. `estimate` remains a dominant
motion diagnostic. The probe publishes these as `shift` and `dominantShift`,
respectively, and only `shift` enters acceptance statistics. Conflicts are checked
without a prior as well, so a narrow prior cannot conceal a different sidebar
shift. Unchanged low-detail blank bands are excluded from textured fixed-sidebar
conflicts. This does not crop an inferred rectangle or implement stitching.

The canonical build passed; targeted ScrollCapture suites passed 2/2 (0.39 s),
including the narrow-prior/sidebar regression. No product capability was enabled.

Actual Windows display scaling was changed to 100% and 150% for the following
runs; it was visibly restored to **125%** afterward. Notepad's earlier accidental
10% text zoom was visibly restored to **100%** before measurement.

| Application/case | Scaling | Accepted / 10 | Visual settle p95 | Verdict |
| --- | --- | --- | --- | --- |
| Chrome issue page, wheel120 | 100% | 2 | 292 ms | Fails whole-region acceptance; five sidebar conflicts explicitly ambiguous |
| Settings Home, wheel40 | 100% | 0 | 390 ms | Fails alignment; every sample settled |
| Settings Home, wheel40 | 150% | 0 | 294 ms | Fails alignment; every sample settled |
| Notepad synthetic document, wheel120 | 100% | 0 | 409 ms | Fails alignment; every sample settled |
| Notepad synthetic document, wheel120 | 150% | 0 | 409 ms | Fails alignment; every sample settled |
| Explorer Home, wheel40 | 100% | 0 | 347 ms | Fails alignment; verified target process explorer |
| Explorer Home, wheel40 | 150% | 6 | 333 ms | Fails ten-repeat criterion; successes 31-32 px, SD 0.548 among six successes only |
| Qt Assistant document | 100%, 150% | not injected | n/a | `no_scroll_area`; explicitly unsupported for sampled document |

Notepad's SetScrollPercent 0/100 endpoints were also sampled at both DPIs; see
raw records rather than treating a zero image match as an endpoint failure.
Explorer's first 100% attempt hit a wrong window after a desktop click was
rejected. `Explorer100 top` and `Explorer100 wheel40` are **excluded**. The later
`Explorer100 verified wheel40` record identified explorer and is the scored run.
At 150% a click reported unavailable geometry, but the already-positioned cursor
and activation still produced an explorer-owned UIA target in both probe records;
the native target metadata is retained for audit. No invalid-target PNG is shared.

### Completed browser follow-up

Browser control recovered. Ordinary and nested documents were measured in Chrome
and Edge at 100% and 150%; Edge also has 125% samples. Each table cell represents
ten inputs. `Accepted` means conservative whole-region `shift`, not `dominantShift`.

| Case | 100% accepted; p95 | 150% accepted; p95 |
| --- | --- | --- |
| Chrome plain, wheel120 | 0/10; 249 ms | 0/10; 301 ms |
| Chrome nested inner, wheel120 | 0/10; 293 ms | 0/10; 263 ms |
| Chrome nested outer, wheel40 | 0/10; 249 ms | 0/10; 299 ms |
| Chrome sticky/sidebar, wheel120 | 2/10; 292 ms | 1/10; 268 ms |
| Edge plain, wheel120 | 0/10; 287 ms | 0/10; 298 ms |
| Edge nested inner, wheel120 | 0/10; 296 ms | 0/10; 263 ms |
| Edge nested outer, wheel40 | 0/10; 256 ms | 0/10; 300 ms |
| Edge sticky/sidebar, wheel120 | 2/10; 302 ms | 2/10; 269 ms |
| Chrome native PDF main document, wheel120 | 10/10; 340 ms | 10/10; 312 ms |

Edge 125% plain/inner/outer: 0/10 each, p95 254/295/302 ms. Chrome PDF 125%:
7/10, p95 303 ms. All 28 scored series (280 inputs) settled. PDF accepted shifts
were 100/125/150 physical pixels respectively, SD 0; the 125% SD describes only
seven successful samples and does not satisfy the ten-repeat criterion.

The plain-page dominant motion was found 10/10, but the rightmost band containing
the scrollbar remained unresolved. Inner-div failures overlap bands obscured by
the desktop automation cursor halo. Outer/sticky content additionally has
conflicting motion. These are conservative rejections with instrumentation
limitations, not proof that the applications cannot support scroll capture.
No failure was converted into a pass by cropping away a band or substituting
the dominant estimate. PDF runs placed the cursor in blank main-document space;
different cursor placement and content mean the PDF DPI results alone do not
establish a fractional-scaling defect.

The synthetic inner div is 720x450 CSS pixels; observed logical UIA dimensions
were 720x450 at 100%, and 721x451 at 125%/150%. This checks dimensions only,
not the full <=2 pt boundary-position criterion. Scored nested runs report two
levels; some began mid-document. The initial `Edge nested100 outer reset` found
no area, and `Edge nested100 inner reset` selected the root with one level;
neither is evidence of a successful nested reset.

The PDF fixture is Microsoft's public 98-page *Engineering Software for
Accessibility* book, SHA256
`a3dd4b0e9c998f827b4533268da37388e6a475271e0c7fda38b6de23a645550f`.
Chrome's native viewer was measured at document zoom 100%. Edge's installed
Adobe extension remained on `Loading...` after reload; this is an environment
block, not a UIA unsupported verdict. Chrome PDF is supplemental coverage and
does not satisfy the specifically requested Edge PDF/Acrobat case.

### Remaining exclusions and unmet requirements

| Item | Status |
| --- | --- |
| VS Code, Teams/Slack, JetBrains/Flutter | Not found in inspected host locations; user-approved environment exclusion |
| Word/Excel | Office activation/license gate; user-approved environment exclusion |
| Edge PDF | Existing Adobe extension stuck loading; no extension/security settings changed |
| Windows Terminal | Native terminal UI automation prohibited by the computer-use skill |
| Elevated/UIPI | Requires separate manual elevated-window validation |
| Full independent <=2 pt bounds calibration | Not established; synthetic dimensions alone insufficient |
| Capture overlay exclusion | Source path reviewed; independent capture proof not established |
| Cold Chromium accessibility-tree readiness | Not established; desktop helper can warm accessibility |
| Inspect level-switch / measure hotkeys | Not implemented; failed requirement |

After the final Edge sticky/sidebar 150% run, Windows Settings visibly showed
**125%** again. The Edge page's sticky issue header was approximately 54 logical
pixels tall in the observed window; this is a visual estimate, not calibrated
boundary ground truth.
Notepad text zoom remains 100%. This follow-up changes measurements/documentation
only; no new C++ change or capability enablement. Earlier full-suite results
remain as recorded below; a flaky performance-test rerun does not turn the
preceding full run into an all-green run.

Thus the host has no established all-criteria application acceptance in this
matrix. Keep capability off; do not infer weighted coverage or an 80% go decision
from dominant shifts or environment exclusions.

Evidence: `build/issue265-matrix/results.jsonl`, `validated-series.json` (28 series,
supersedes the earlier partial `summary.json`), per-case PNGs,
`build/issue265-matrix-build.log`, and `build/issue265-matrix-tests.log`.
Final host follow-up posted at [issue comment](https://github.com/victorfu/snap-tray/issues/265#issuecomment-6761656590).

## Delta 120 follow-up: delayed UIA prior and sidebar motion

This follow-up supersedes the earlier delta-120 residual finding below. Changes
are local and uncommitted on `fe07efa0`; the product capability remains disabled.

Published follow-up and extracted per-sample evidence:
[issue #265 comment](https://github.com/victorfu/snap-tray/issues/265#issuecomment-6756791181).

### Evidence and fix

A diagnostic run sampled twelve additional frames/positions after visual settle
for each of six wheel-120 inputs. **All 72 tail frames were identical to the
settled frame**, while UIA subsequently changed from intermediate offsets (often
only about 2 physical pixels of progress) to the final position. Visual settle
therefore did not imply accessibility-position freshness. The temporary tail
instrumentation was removed after collecting the evidence.

The sidebar is a separate spatial issue: after the fourth input, text moves
125 px while a sidebar segment moves about 115 px; subsequent sidebar segments
remain stationary. Waiting longer does not make those columns share a shift.
The desktop automation cursor/highlight also obscured some main-content rows;
a diagnostic run with that overlay over the body accepted 7/10. Moving the
highlight to the sidebar improved coverage without modifying captured pixels.
All intermediate failures remain in the raw JSONL.

- The core now offers eight-band vertical matching with a strict majority of
  **all eight** bands (at least five), retaining the existing exact-row thresholds.
  Equal competing panes remain ambiguous; blank/failed bands do not reduce the
  majority requirement. Static-band majorities cannot declare moving frames
  unchanged. This estimates a dominant displacement, not a stitchable rectangle.
- The probe checks whether the independent visual displacement lies inside the
  actual platform prior's existing search window. Otherwise it polls the API,
  services the STA/application event queue and verifies the frame/bounds remain
  unchanged, with a bounded additional timeout. No pixel-derived prior, wider
  search radius, or previous-repeat displacement is substituted.
- The first implementation required agreement within 2 px, which was stricter
  than the prior's contract and rejected one valid delta-40 sample. The final
  check uses the configured search radius (64 px), preserving approximate priors.
  Raw initial and final positions, wait samples, and failures remain visible.
- Schema 2 explicitly identifies `vertical_band_majority`, all band results,
  `nonUniformMotion`, the legacy `fullRowShift`, and `positionSync`. The latter
  separately records total time from dispatch; visual settle timing is unchanged.

### Final native retest

Chrome 156.0.8078.80, same GitHub issue page, Windows 11 26H2, 125% scaling,
1314 x 814 captured pixels. Each series starts at the top; no build/test suite
runs concurrently. All 30 injections and visual settles succeeded.

| Method | Dominant shifts accepted | Shift | Sample SD | Visual settle p95 | Prior-window confirmation p95 |
| --- | --- | --- | --- | --- | --- |
| Wheel delta 120 | **10/10** | 125 px | 0 px | **292 ms** | **658 ms** |
| Wheel delta 40 | **10/10** | 41-42 px | 0.483 px | 297 ms | 391 ms |
| UIA SmallIncrement 1 | **10/10** | 50 px | 0 px | 98 ms | 198 ms |

Delta 120's legacy whole-row matcher still accepts only **1/10** on these exact
final pairs. The 10/10 result is explicitly dominant-band matching, not a claim
that one displacement aligns the whole viewport. Samples with a fixed/differently
moving sidebar retain the conflict flag. Bounds still include sidebar and sticky
header, so this does **not** pass issue #265's complete application acceptance
criterion. Total confirmation latency also exceeds 500 ms for delta 120 even
though visual settle is below 500 ms. The 100%/150% and other-application matrix
remain unverified; keep `supportsScrollCapture` disabled.

### Verification and local artifacts

- Canonical Windows Debug build passed (`scripts/build.bat`).
- ScrollCapture suites: **2/2 passed**, 0.41 s, including new uniform/sticky/different
  sidebar, equal-pane ambiguity, wrong prior, invalid inset, blank-majority,
  unchanged-frame and unrelated-content regressions.
- `git diff --check` passed. This follow-up did not rerun the 16-minute full suite;
  its preceding result and isolated performance rerun remain documented below.
- `build/issue265-delta120.jsonl`: complete diagnostic, intermediate and final runs.
- `build/issue265-delta120/{trace,verified-delta120,verified-delta40,verified-pattern1}/`:
  raw lossless images, including all 72 tail frames.
- `build/issue265-delta120/verified-summary.json`: extracted final sample fields.
- `build/issue265-delta120-build-verified.log`, `build/issue265-delta120-tests.log`:
  final build/test logs.

## Chrome matching fix and same-machine retest

The follow-up below uses a local, uncommitted patch on that baseline. It fixes
the original Chrome UIA SmallIncrement **0/10** result; it does not complete the
whole application/DPI matrix or enable the product capability.

Published results and all three raw retest JSONL records:
[issue #265 comment](https://github.com/victorfu/snap-tray/issues/265#issuecomment-6756426103).

### Cause and change

Lossless before/after pairs reproduced the failure. The last two image columns
contained Chrome's stationary window-frame gradient. Full-width row hashing
compared that gradient at different vertical positions when testing the correct
50 px shift, invalidating otherwise matching rows. Accessibility bounds and
rounding through integer logical coordinates can include this narrow perimeter.

On three freshly captured failing pairs, replaying the same pixels with a
horizontal inset increased the match ratios from **3.0%, 3.9%, 3.8%** to
**53.0%, 66.4%, 65.1%**. The candidate remained 50 px. The prior run's original
ten records had approximately 15% matching rows; their PNGs were not saved, so
the paired pixel analysis uses the fresh reproducer rather than claiming those
original images were replayed.

- `computeRowHashes` now accepts an optional physical-pixel horizontal inset;
  its default remains zero for existing callers and settle detection.
- The probe matches inside a 2-logical-pixel horizontal perimeter, rounded up
  to physical pixels (**3 px per side at 125%**), and records
  `hashHorizontalInsetPx`. Full captured images and bounds are preserved.
- Match ratio, distinct-row and ambiguity thresholds remain unchanged. No
  sticky-region detection or fallback to a guessed prior was introduced.
- `--frames-dir` saves original before/after PNGs after settle timing, with paths
  or an explicit save error in JSON. This makes future failures reproducible.
- Regression cases cover a stationary gradient border, invalid/empty insets,
  detail-free interiors and preservation of conflicting-shift ambiguity.

### Native retest (no concurrent test suite)

Chrome `156.0.8078.80`, same issue page and 125% display, from the top for each
series. Each series saved raw frame pairs. All 30 scroll injections succeeded
and all 30 samples settled.

| Method | Accepted shifts | Measured shift | Sample standard deviation | Strict settle p95 |
| --- | --- | --- | --- | --- |
| UIA SmallIncrement, amount 1 | **10/10** | 50 px every time | **0 px** | **141 ms** |
| Wheel delta 40 | **10/10** | 41–42 px; mean 41.7 | **0.483 px** | **290 ms** |
| Wheel delta 120 | **2/10** | 125 px for the two accepted samples | 0 px **among successes only** | 306 ms |

SmallIncrement and wheel40 meet the displacement and settle thresholds for
these sampled conditions. This is not a claim that the entire Chrome matrix
meets all four issue criteria; bounds calibration and 100%/150% remain pending.

The wheel120 series remains a failure, not 125 px with proven repeatability:
several UIA priors were approximately 2 px while the unconstrained matcher found
125 px. Samples 1 and 3 (zero-based) had unconstrained `found` results but fell
outside the prior's ±64 px search range. Samples 4–9 also failed the unchanged
row-match ratio without a prior. Saved pairs show independently changing page
content: sample 4's main text votes for 125 px, while sidebar rows vote around
115–116 px. The fixed header and cursor highlight are also present. These are
separate residual cases; this fix does not widen the search or silently accept
the unconstrained candidate.

### Validation and evidence

- Windows Debug build: passed.
- ScrollCapture suites, including new regressions: **2/2 passed**, 0.38 seconds.
- Canonical `scripts/run-tests.bat`: **247/248 passed**, 1001.65 seconds. The
  sole failure was the unchanged `RecordingManager_RecordingAppearance::costAt4K`
  performance limit: blurred-video composition took 296.6 ms/frame against
  250 ms. After the suite finished, an isolated rerun passed all 21 assertions
  (1/1 CTest, 3.87 seconds), measuring 201.4 ms/frame. No threshold or recording
  code was changed. The full run's failure is retained rather than relabelled
  as an entirely green run.
- `build/issue265-fix.jsonl`: baseline reproducer, resets and all three retests.
- `build/issue265-fix/{before,after,wheel40,wheel120}/`: lossless frame pairs.
- `build/issue265-fix/pixel-analysis.json`: same-image comparison and SHA-256.
- `build/issue265-fix/changes.patch`: source/test/tool diff; SHA-256
  `1dabe0ecc72979854f06a3b2c3bc91e58ae073123791614b1b823a6849aa921e`.
- `build/issue265-fix-build.log`, `build/issue265-fix-unit.log`,
  `build/issue265-fix-full-tests.log`: build/test logs.
- `build/issue265-performance-recheck.log`: isolated performance-suite rerun.

## Initial acceptance verdict (before the fix)

**Acceptance remains incomplete; keep `supportsScrollCapture` disabled.**
Build and automated tests pass. Native application sampling produced usable
accessibility diagnostics, but no validated pixel-shift repeatability result.
The observations below do not establish weighted coverage or justify a
continue/stop product decision.

## Initial environment and automated checks

- Windows 11 26H2, kernel `10.0.26300`; Qt 6.12.0 MSVC Debug.
- One 1920 × 1200 display, 1536 × 960 logical, **125% scaling**.
- Wheel scroll lines: 3; mouse wheel routing: 2; probe integrity: 8192.
- `scripts/build.bat`: passed; all targets already up to date.
- ScrollCapture suites outside the sandbox: **2/2 passed**, 0.24 seconds.
- Full suite outside the sandbox: **248/248 passed**, 997.38 seconds.
  Qt bin was prepended to `PATH`; CTest retained repository test timeouts.
- The earlier sandboxed full run was stopped after reaching test 205. Its
  numerous temporary-file access failures are not a clean regression baseline.

Local evidence (under the ignored build directory):

- `build/issue265-build.log`
- `build/issue265-scroll-tests.log`
- `build/issue265-tests-unsandboxed.log`
- `build/issue265-acceptance/results.jsonl` — original probe records
- `build/issue265-acceptance/REPORT.txt` — detailed run notes

## Exploratory native observations at 125%

These samples overlapped the full test run. Their latency and focus-sensitive
behavior are provisional. A reported candidate `shiftPx` is **not** an accepted
shift when its status is `not_found` or `insufficient_detail`.

| Target | Detection / position | Scroll observations | Assessment |
| --- | --- | --- | --- |
| Windows Settings Home | First query found XAML ScrollViewer; viewport visually excludes title/sidebar. SetScrollPercent 100/0 reported atBottom/atTop correctly. | Wheel delta 40: 10 injections and settles, strict p95 369 ms, 0/10 accepted shifts. SmallIncrement amount 1: 10 settles, p95 433 ms, 0/10 accepted shifts. | Not accepted; pixel repeatability unproven. |
| File Explorer Home content | First query found XAML ListView and position. | Wheel delta 40: 10 settles, p95 366 ms, 0/10 accepted shifts; some API offsets did not change. | Not accepted; needs controlled rerun. |
| Qt Assistant 6.12 document | `no_scroll_area` after three attempts, both before and after a visible manual scroll. | Document visibly moved with manual wheel input. | Explicit unsupported result for this sampled Qt document; not a supported application. |
| Notepad 11.2607.14.0 | First query found RichEditD2DPT Document, atTop true; newly created 300-row synthetic text tab. | Wheel delta 120: 10 injections/settles, p95 603 ms, 0/10 accepted shifts. | Provisional; needs controlled rerun. |

Bounds were checked visually, **not calibrated to the ≤2 pt threshold**.
No valid measured-shift standard deviation can be reported with zero accepted
shifts. The first Settings pattern test accidentally used default amount 120
and reached the bottom after one repetition; it is excluded from repeatability
assessment. The subsequent record explicitly uses amount 1.

### Invalid isolated rerun

After the full suite finished, a further Notepad record was collected with label
`Notepad wheel120 125pct isolated after full suite`. **Exclude it from scoring.**
The desktop helper repeatedly reported `user input was detected in this window`,
including after fresh observations and the user's confirmation to continue.
The subsequent screenshot showed text zoom changed from 100% to 10%, so wheel
events did not constitute the intended scroll experiment. The attempted zoom
reset also returned the input guard error; restoration was not confirmed.
This is an automation failure, not evidence of a SnapTray scrolling regression.

## Remaining acceptance work and blockers

### Chrome retry after the full test suite

The Chrome URL-observation blocker cleared on retry. Chrome `156.0.8078.80`
displaying the issue page was located on the first probe query, with a 1051 ×
651 logical viewport at (479, 87). This excludes the browser toolbar. The page
has a sticky issue header, approximately 55 logical pixels high in the observed
scrolled screenshot; this is an estimate, not calibrated ground truth.

Ten UIA SmallIncrement operations (amount 1) returned success and visibly
advanced the document. All ten settled, strict p95 **241 ms**, but all ten
pixel-shift statuses were **`not_found`**. The API reported approximately
50.05 physical pixels for six samples and zero for four. Although the estimator
retained a 50-pixel candidate, none passed its matching criterion, so neither
that candidate nor the API displacement establishes actual pixel accuracy.
SetScrollPercent 100/0 correctly returned atBottom/atTop, and the page was
returned to the top.

These measurements ran after CTest completed and used UIA patterns, avoiding
keyboard-modifier-dependent wheel input. Chrome's accessibility tree had
already been read by the desktop helper, so the first-query result does **not**
prove cold-start tree readiness. The keyboard/mouse input guard remained
unreliable; no browser wheel experiment or nested-page navigation was counted.

### Outstanding matrix

- 100% and 150% display scaling; the current host was sampled only at 125%.
- Browser ordinary/nested pages, Edge and cold UIA query counts. The Chrome
  sticky issue-page sample above is only one case. Browser-connected automation
  is unavailable in this chat.
- Electron editor/sidebar, Office/PDF, elevated-process UIPI and JetBrains/Flutter
  negative cases. Word opened an unaccepted license agreement and identified
  itself as unlicensed; no agreement was accepted. Excel was not tested.
- Terminal coverage requires a manual operator: the installed computer-use
  skill prohibits terminal UI automation.
- Independent bounds calibration, overlay capture exclusion, nested selection,
  and controlled ten-repeat measurements without competing test/UI input.
- Source inspection found `inspect` uses a fixed `--level` and measurement is a
  separate command; the issue's keyboard shortcuts for changing level and
  triggering measurement are not implemented in `tools/scroll-probe/main.cpp`.
- Overlay code calls `setWindowExcludedFromCapture` and hides before measuring;
  this source check does not independently validate Windows capture exclusion.

In the initial acceptance run no product code was changed and no GitHub comment
was posted. The Notepad test
tab remains open; its observed text zoom is 10% and may need manual restoration
to 100% before further measurements.
