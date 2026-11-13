# macOS scroll-probe continuation — 2026-11-12

Issue: [#265](https://github.com/victorfu/snap-tray/issues/265).
Measurement branch: `codex/verify-macos-scroll-265`. **Partial validation; not acceptance.**

This is a historical snapshot of the November 12 measurements, not the current
implementation status. The endpoint fix is tracked in [PR #316](https://github.com/victorfu/snap-tray/pull/316);
inspect shortcuts were subsequently added in [PR #317](https://github.com/victorfu/snap-tray/pull/317).
Neither change retroactively validates these measurements.

Publication preparation (2026-11-13) rebased the fixed-point probe option onto
`d88d2354`. The canonical build, probe build and six focused ScrollCapture/platform
tests passed (1.33 seconds). Six invalid CLI cases were rejected; positive and
negative explicit coordinates and default cursor metadata were checked without
scroll injection. All JSON files parsed and all 180 local PNG hashes matched the
manifest. These checks do not constitute a new application measurement run.

## Versions and verification

The first measurements used local `a9cbee7d` (AXWebArea endpoint fix, schema 1).
The branch was then fast-forwarded to remote main
`d603d154d7700d5beaee4cd6de5411677d433f3b` (schema 7). Both sets are preserved,
not combined into a single success rate. The macOS platform implementation and
`MacScrollPosition.h` are identical between these revisions; image estimation
and probe diagnostics are different.

The only code change in this continuation is the developer probe's optional
`measure --point x,y`: explicit global logical coordinates, recorded separately
from the actual cursor, used for screen metadata and AX re-queries. It neither
activates windows nor bypasses occlusion. The original cursor is restored after
scrolling. No product capability or estimator threshold changed.

- Canonical `scripts/build.sh` and probe/test target builds passed on latest main.
- Latest focused tests: **6/6 passed**, 2.34 seconds; [tests-main.log](tests-main.log).
- Four CLI rejection checks passed: inspect mode, non-integer, extra component,
  and integer overflow; [point-cli-checks.json](point-cli-checks.json).
- Earlier schema 1 tests: 4/4 passed; [tests.log](tests.log).
- No full-suite rerun is claimed. `git diff --check` passed.
- macOS 27.0.1 (26A434), built-in Retina display, 1470 × 956 points, DPR 2;
  Automatic/overlay scroll bars. Host AX/event/capture permissions all true.
- Safari 27.0.1; Chrome 157.0.8103.40; Finder 27.0; System Settings 1.0; Word 16.115; Preview 11.0; Notes 4.13.

## Latest-main results (schema 7)

Raw [results-schema7.jsonl](results-schema7.jsonl), combined metadata
[summary.json](summary.json). Lossless before/after PNG files remain locally in
`build/issue265-macos-20261112/frames/`; JSON records their paths.

| Case, pixel wheel 120 points ×10 | Accepted whole-area shifts | Motion / strict settle | Strict settle p95 |
| --- | --- | --- | --- |
| Chrome plain repeated prose | 0/10 (`not_found`) | 10/10 | 163 ms |
| Chrome nested div, selected root | 0/10 (`ambiguous`) | 10/10 | 150 ms |
| Chrome dense eight-column control | 10/10, all 240 physical px, SD 0 | 10/10 | 143 ms |
| Safari dense, ordinary wheel | 10/10, all 240 physical px, SD 0 | 10/10 | 131 ms |
| Safari dense, continuous wheel | 10/10, all 240 physical px, SD 0 | 10/10 | 132 ms |
| Safari nested div, selected root | 0/10 (`ambiguous`) | 10/10 | 143 ms |
| Word synthetic RTF | 0/10 (9 `not_found`, 1 `ambiguous`) | 10/10 | 205 ms |
| Preview synthetic PDF | 0/10 (6 `not_found`, 4 `ambiguous`) | 10/10 | 137 ms |
| Notes synthetic text | 0/10 (`not_found`) | 10/10 | 122 ms |

Chrome exposes no platform position/endpoints in these cases. The dense result
passes repeatability and settle timing only; it does not meet all four acceptance
criteria. Plain content has repeating/blank bands: the first sample has only
three independently successful bands, insufficient for the five-band threshold.
This is a conservative content-detail rejection, not proof Chrome cannot scroll.
The dense fixture uses different text in every column, keeping the rejected
plain fixture intact as a distinct case.

At the nested point Chrome reports only the root `AXScrollArea` and one level.
`AXManualAccessibility` returned -25205; re-query still found only one level with
unknown position. Enhanced UI was already on; this probe did not enable it.
No Electron cold-start/restart conclusion follows from this warm Chrome run.

`Safari dense schema7 target` actually hit Chrome and is **excluded**. Its record
is retained to prevent a label from being mistaken for verified app identity.
A subsequent user handoff correctly identified Safari and enabled the three
series above. Safari dense exposes a scrollbar fraction but no absolute offset;
its image estimates have no platform offset prior. Nested still exposes only
the outer area; its fraction stays zero while inner content moves. Schema 7
rejects all ten pairs instead of accepting schema 1's single shift.

`Word RTF schema7 phase1` hit ChatGPT and is also excluded. A later handoff correctly acquired Microsoft Word 16.115 and measured the
synthetic [RTF document](document.rtf). No existing user documents were used as
test content. Word exposes `AXLayoutArea` contents, 8766pt content height and a
751pt viewport at the observed 156% document zoom. All ten injections moved
and settled, but none passed alignment. Four initial API shift observations
were zero despite observed motion; six were approximately 240px. This is an
observation, not a proven diagnosis of the timing/position source. Endpoint
probes returned fraction 1 / offset 8015 / atBottom at the bottom, then fraction
0 / offset 0 / atTop on return. The bottom screenshot showed the blank lower
page margin, not an independent final-text-row check.

Excel synthetic Book1 contains A1:L150, using text cells `row-column-product`
where product is `(row+2)*(column+16)` (one-based indices). At 100% zoom, both
initial and post-PageDown probes returned `no_scroll_area` after three attempts.
PID 74624 was independently verified as Microsoft Excel. PageDown moved the
active cell to A38 and exposed rows through 75, proving worksheet navigation
works. The current locator did not expose a usable scroll area at `(650,500)`;
no probe scroll events were sent. This is not an all-points/all-versions claim.
The synthetic workbook remains open as unsaved Book1.

Preview loaded the 12-page [synthetic PDF](preview-fixture.pdf), generated with
ReportLab and visually checked after Poppler rendering. Its first probe hit
ChatGPT and is excluded. A later handoff acquired Preview 11.0: AX read a
17179.1465pt document and 827pt viewport. The ten API deltas were approximately
240px (floating-point rounding only), but all alignment results were rejected.
At the bottom, AX fraction was 0.999991 and `atBottom=true`; independent app
state showed Page 12 of 12. Returning to the top produced fraction approximately
zero and `atTop=true`. The desktop tool
took approximately 19 minutes to return from selecting Preview; that wait is
not a scroll-settle sample.

Notes was tested in a separate window containing a newly created synthetic
180-row note, titled `SnapTray issue 265 synthetic scroll fixture`. Only this
window's content region was saved as PNG; existing notes were not used as test
content or written to the report. AX exposes `AXTextArea` document geometry and
a scrollbar. All ten injections/motions/strict settles succeeded; all ten
alignment estimates were rejected. The bottom sequence needed two events and
returned fraction 1 / `atBottom=true`; document height changed from 2991pt to
3085pt during the run. The top sequence returned fraction 0 / offset 0 /
`atTop=true`. No independent stable-content-height claim is made. The synthetic
note remains in Notes for reproducibility.

Nine valid schema 7 ten-repeat sequences contain **90 injections with motion and
strict settle**. Three sequences pass alignment repeatability; this aggregate is
not weighted application coverage. The 180 before/after PNGs (about 89 MiB) stay
under the ignored build directory. [frame-manifest.json](frame-manifest.json)
records their paths, sizes and SHA-256 hashes; raw JSON and fixtures are in this
report directory. The results summary and endpoint evidence excerpt were posted to
[issue #265](https://github.com/victorfu/snap-tray/issues/265#issuecomment-6813719678).
This directory publishes the report, structured measurements, test logs and
synthetic fixtures. The PNG files remain local and are not downloadable from
these paths. User-specific repository prefixes in JSON and logs were normalized
to repository-relative paths; measurement values and image hashes are unchanged.

## Earlier measurements (schema 1; not latest image-matcher acceptance)

Raw [legacy-schema1-results.jsonl](legacy-schema1-results.jsonl).

| Case | Observation |
| --- | --- |
| Safari plain wheel 120 ×10 | 10 accepted shifts, all 240px, SD 0; strict p95 136ms |
| Safari plain continuous 120 ×10 | 10 accepted shifts, all 240px, SD 0; strict p95 132ms |
| Safari plain PID delivery 120 ×10 | 10 unchanged/0px results, zero motion; **not successful scrolling** |
| Safari plain endpoints | AX fraction 1 at bottom, 0 at top; guards stopped subsequent events. Last row 0249 independently visible at bottom |
| Safari nested selected root ×10 | 10 image shifts of 240px, while root AX fraction stayed 0; **wrong-area attribution**, not nested acceptance |
| Safari nested level 1 | `no_scroll_area`, only one level after three attempts |
| Finder src list wheel 40 ×10 | 10 injections/motions/strict settles, AX delta 80px each; 0 accepted image shifts; p95 113ms |
| System Settings General main point | `no_scroll_area`; no events injected. Does not establish all main-content locations unsupported |

### Confirmed native-position counterexample

System Settings sidebar returns `AXOutline` contents with the same 527pt height
and origin as its viewport, despite a visible scrollbar thumb ratio near 0.51.
At the initial top it reports `contentHeight=527`, `offset=0`, and **both**
`atTop=true` and `atBottom=true`. An independent computer-use scroll changed the
scrollbar fraction to 0.953287; the next probe still reported both endpoints true
and zero offset. This contradicts the independent scrollbar value and actual
scrolling. The false-zero-range inference is not confined to `AXWebArea`.
No fix was present during these measurements. At that time, main's relevant
platform code was unchanged from the measured revision; see PR #316 above for
the subsequent fix. An upward scroll was performed afterward;
its observed scrollbar value was 0.08037, not an exact restoration to zero.

### Safari nested targeting counterexample

At a point inside the 1100 × 750 CSS-pixel div, AX walking returns the outer
1324 × 871pt page region. Ten events move the div contents to roughly row 0032,
while its red border and the outer page's row 0000 remain visible in place.
The selected outer AX position remains fraction 0 throughout. Schema 1 accepting
240px cannot be interpreted as correct outer-area scrolling or correct bounds.
Latest schema 7 now rejects all ten Safari nested pairs as `ambiguous`, but
this does not resolve the missing inner AX target or establish correct bounds.

## Desktop limitations and remaining matrix

Computer-use can show and operate an app without leaving it frontmost for the
host probe. [environment.jsonl](environment.jsonl) preserves sandbox permission
failures and wrong-window checks. No scroll phase was started after a known
wrong-target check. A first user handoff enabled Safari measurements. Later
Safari switching still hit Chrome, then ChatGPT after minimizing Chrome.
Computer-use explicitly forbids operating `com.openai.codex`, so another user
foreground handoff was requested; that restriction was not bypassed.

| Remaining item | Status |
| --- | --- |
| Safari schema 7 | Dense ordinary/continuous and nested sequences completed; sticky page and independent bounds pending |
| Finder schema 7; Settings conflict retest on schema 7 binary | Earlier evidence retained; latest image results pending |
| Word | Ten-repeat and endpoint probes complete; alignment fails; independent boundaries pending |
| Excel | Two verified-process `no_scroll_area` probes; PageDown control performed |
| Preview PDF | Ten-repeat and endpoint probes complete; alignment fails; independent boundaries pending |
| Notes | Synthetic separate-window sequence and endpoint probes complete; alignment fails |
| Mail | Add Account screen; no configured mailbox. Cancel closed the app; environment blocked |
| Terminal | Computer-use explicitly prohibits `com.googlecode.iterm2`; not tested |
| Qt Assistant | Two `AXError.cannotComplete` observations; host target check hit ChatGPT and was excluded. Environment/tool blocked |
| DataGrip counterexample | Computer-use access not approved; not tested |
| VS Code | Not in inspected `/Applications` listing; not an unsupported-app verdict |
| Slack | **Excluded at user's explicit request** |
| Always-visible bars, non-Retina, permission first-use | Not tested in this continuation |
| Overlay independent capture-exclusion proof | Not tested |
| Inspect keyboard shortcuts | Still absent in tested revision |

Fixtures: [plain](plain.html), [nested](nested.html), [dense](dense.html).
Each has 250 rows of 37 CSS pixels including border. Nested content box is
1100 × 750 CSS pixels, 2px border, 40px margin. Dimensions alone do not certify
AX screen-coordinate error ≤2pt. Full compilation/tests were not running during
scroll measurements. The initial schema 1 wheel/continuous/PID runs used successive
sections, not identical starts; no controlled speed ranking is claimed.

Keep capability disabled. Native target/position misreports and pending criteria
prevent a final weighted-coverage or continue/stop decision.
