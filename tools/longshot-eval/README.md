# Offline longshot evaluator

Build with tests enabled: `cmake --build build --target longshot-eval`.

```
build/bin/longshot-eval --video recording.mp4 --ground-truth page.png --start 0 --end 5000 --crop 0,0,640,480 --output /tmp/evaluation
build/bin/longshot-eval --manifest tests/Longshot/corpus/manifest.json --output /tmp/corpus-evaluation
```

The manifest has a `cases` array. Each case accepts `id`, `video`, `groundTruth`, optional `sha256`, `startMs`, `endMs` (-1 for end) and `crop` ([x,y,width,height], or empty for the full frame). Paths are relative to the manifest. Ground truth must represent exactly the expected visible output, including its column span and complete row range. Additional platform/codec/capture provenance fields remain in the manifest for review.

Each numbered output directory contains PNG parts; `report.json` records dimensions, breaks, duplicated/missing/misaligned/unmatched rows, elapsed time and decode-cache bytes. On Unix, peak RSS is the whole process high-water mark, not per-case memory. Strict automated acceptance requires zero row errors, exact height and no gaps. No ground truth means manual review, not success. Codec-domain differences can cause strict evaluation failures and require independently checked references; do not loosen thresholds to hide failures.

Exit codes: 0 = all cases passed, 1 = evaluation failed or requires manual evidence, 2 = invalid command/manifest or report write failure. The evaluator does not record screens. It is a developer test tool, not an application recording command.

## Native corpus collection

The checked-in manifest enumerates 24 missing cases: web, chat, IDE, table, settings and PDF, each with steady, fling, reverse and pause motion. Use dedicated non-personal content in the actual applications. Save recordings and independent references outside Git, then populate the manifest paths and metadata. Include fixed headers, sidebars and dynamic content across the cases. A synthetic viewport is not a substitute for recording the application.

Record OS, hardware, encoder, actual dimensions/fps, crop and trim; calculate SHA-256 of each video. For web, use a browser full-page screenshot as the reference. For other applications retain source documents and independently verified expected ordering; leave groundTruth empty where pixel scoring cannot be justified, and retain explicit manual-review status. The default missing manifest intentionally fails.
