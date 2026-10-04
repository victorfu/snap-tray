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
- [ ] Longshot preview controller, cancellation and results UI.
- [ ] PNG / Copy / Pin, retaining the source recording.
- [ ] Review evidence refresh and cross-platform checks.
- [ ] Translations, documentation, full build/tests and QML lint.

## Hardware acceptance matrix

Run on macOS and Windows with actual 5K and 6K displays: record with audio, verify decoded dimensions/fps/codec, playback, unedited save, full-size trim, crop, smart-save re-encode and source retention on failure. Record the hardware, OS, encoder mode, output dimensions and evidence paths. Synthetic inputs do not prove screen capture capabilities.

Additional unavailable environments must retain explicit gaps: Windows 1909 and 2004+ exclusion, native OCR language packs, multi-display scaling, clean TCC identity, USB/Bluetooth disconnect and Application Verifier. No skipped check is a pass.

Evaluator verification: native SyntheticHarness passed after rebuilding the stale binary; missing-corpus manifest exits 1 and records all 24 unavailable cases. CLI built successfully.
