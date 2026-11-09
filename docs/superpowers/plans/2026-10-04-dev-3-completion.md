# dev-3 completion evidence (2026-10-04)

Baseline: `7afc5b80`. The working tree was clean. This document tracks Phase 4b; the existing review tracker remains authoritative for review IDs.

## Baseline

- Phase 1: crop, native transcoders and export paths exist; runtime matrix is separate from source completion.
- Phase 2: window timeline, sidecar and snapping exist.
- Phase 3: intermediate quality and smart save exist. Actual 5K/6K capture matrix remains unverified.
- Phase 4a: solver, analyzer, renderer, session and native-source tests exist; PR #128 is merged.
- VideoTrimmer removal is complete (`412521e7`).
- Historical plan checkboxes are instructions, not proof of current runtime verification. Do not infer missing implementation solely from unchecked steps.

## Milestones

- [x] Evaluator with strict failure reporting and independent oracle controls.
- [ ] 24 native application recordings and references (manifest enumerates missing cases).
- [x] Longshot preview controller, cancellation and results UI.
- [x] PNG / Copy / Pin, retaining the source recording.
- [x] Review evidence refresh: all 36 Fix Ready IDs mapped to passing local suites.
- [ ] Cross-platform and physical-device checks: original acceptance gates retained.
- [x] 24 translation catalogs, documentation, local build/tests and QML lint.

## Hardware acceptance matrix

Run on macOS and Windows with actual 5K and 6K displays: record with audio, verify decoded dimensions/fps/codec, playback, unedited save, full-size trim, crop, smart-save re-encode and source retention on failure. Record the hardware, OS, encoder mode, output dimensions and evidence paths. Synthetic inputs do not prove screen capture capabilities.

Additional unavailable environments must retain explicit gaps: Windows 1909 and 2004+ exclusion, native OCR language packs, multi-display scaling, clean TCC identity, USB/Bluetooth disconnect and Application Verifier. No skipped check is a pass.

Evaluator verification: native SyntheticHarness passed after rebuilding the stale binary; missing-corpus manifest exits 1 and records all 24 unavailable cases. CLI built successfully.

## Implementation and validation outcome

The preview now provides longshot generation, stage progress/cancellation, tiled result viewing, warning markers with real source-time navigation, header toggle, automatic 30,000-pixel splitting, Save PNG / Copy / Pin and Back. PNG actions preserve the recording; close waits for the worker before source deletion. Saved images use unique atomic publication and valid image-history records, without window sidecars. Solver cancellation and a 512 MiB aggregate output budget bound the new UI work. Preview textures are limited to 2048 pixels on either side.

Local verification:

- Canonical `scripts/build.sh`: passed on macOS 27.0.1 ARM64 / Qt 6.11.2.
- Canonical `scripts/run-tests.sh`: 183 suites executed. The final full run passed 182; the crop suite's stale non-snap coordinates and missing release-coordinate handling were corrected, then `ctest --test-dir build --rerun-failed --output-on-failure` passed. The final crop/longshot UI suite also passed all 67 Qt Test cases.
- `all_qmllint`: exit 0; warnings remain (including context-property/unqualified-access warnings).
- `python3 scripts/check-longshot-translations.py`: all 24 catalogs and placeholder sets passed.
- CI YAML parsed successfully; three platform jobs, dev-3 and manual triggers confirmed. Remote execution is pending a separately authorized push.
- Native SyntheticHarness, Pipeline, Renderer and Session passed. The actual 640×480 result view was rendered and inspected; preview/Pin/Back and retained-source behavior were exercised.
- The 24-case manifest has unique IDs and existing dedicated source documents. All videos remain missing, so the evaluator deliberately returns failure for this manifest. These are capture recipes, not completed recordings.

Remaining gates: Screen Recording permission for the capture process, Windows/Linux native runs, 5K/6K physical displays, old Windows exclusion cases, USB/Bluetooth loss and multi-display behavior. No review ID was promoted to Verified without its missing evidence. See the review tracker and `docs/developer/reviews/2026-10-04-validation-environment.md`.

Milestone commits: `c9e827e3` (evaluator/baseline), `1351f69b` (engine/UI). Final controller regression: 8/8 cases, including two-part save, selected-part pin and retry/history deduplication. Native capture remains blocked as described above.
