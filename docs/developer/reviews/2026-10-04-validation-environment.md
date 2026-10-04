# 2026-10-04 validation environment

macOS 27.0.1 ARM64 / Qt 6.11.2. Native tests were run outside the restricted sandbox; test settings isolation remains enabled. Build cache was moved to `/tmp/snaptray-ccache` because the sandbox cannot write the user ccache directory.

## Skipped checks in the first full run

These are not passes for their underlying acceptance conditions:

- `Platform_LinuxClipboardOwner`: SKIP   : TestLinuxClipboardOwner::survivesCLIExitAndReleasesOwnership() Requires Linux X11
- `Platform_LinuxClipboardOwner`: SKIP   : TestLinuxClipboardOwner::usesAppImageLauncher() Requires Linux X11
- `Qml_RecordingPreviewExport`: SKIP   : tst_RecordingPreviewExport::previewWindowKeepsNativeCaption() Native caption styles are checked on Windows only
- `PinWindow_StyleSync`: SKIP   : TestPinWindowStyleSync::testLinuxBypassPinDoesNotBecomeToolbarTransientParent() Linux-only toolbar transient-parent policy.
- `PinWindow_StyleSync`: SKIP   : TestPinWindowStyleSync::testLinuxBypassPinReceivesSpaceShortcutAfterShow() Linux-only pin focus policy.
- `PinWindow_StyleSync`: SKIP   : TestPinWindowStyleSync::testLinuxBypassPinReceivesSpaceShortcutAfterClickFocus() Linux-only pin focus policy.
- `ScreenCanvas_Placement`: SKIP   : TestScreenCanvasPlacement::testSubToolbarUsesAvailableScreenBoundsNearBottomPanel() Screen does not expose a bottom reserved work area.
- `ScreenCanvas_Placement`: SKIP   : TestScreenCanvasPlacement::testGrabbedDrawingUsesCurrentCursorPositionDuringDrag() System cursor position could not be adjusted for ScreenCanvas drawing test.
- `RegionSelector_AttachmentLayout`: SKIP   : tst_RegionSelectorAttachmentLayout::testRegionControlPanelMovesInsideSelectionWhenTopToolbarOverlaps() Default external region-control placement did not overlap the toolbar in this environment.
- `RegionSelector_TransientUiCancelGuard`: SKIP   : tst_RegionSelectorTransientUiCancelGuard::testDetachedWindowDeactivateGuardIgnoresSyntheticDeactivate() Detached capture deactivation guard is Windows-specific.
- `RegionSelector_StyleSync`: SKIP   : TestRegionSelectorStyleSync::testSelectionBodyHoverUsesEventPosWhenLiveCursorLags() System cursor position could not be adjusted for live-cursor lag test.
- `RegionSelector_StyleSync`: SKIP   : TestRegionSelectorStyleSync::testFloatingToolbarWindowOwnsArrowCursor() System cursor position could not be adjusted for toolbar hover test.
- `RegionSelector_StyleSync`: SKIP   : TestRegionSelectorStyleSync::testToolbarLeaveRestoresArrowToolCrossCursor() System cursor position could not be adjusted for toolbar round-trip test.
- `RegionSelector_StyleSync`: SKIP   : TestRegionSelectorStyleSync::testToolbarLeaveRestoresSelectionBodyMoveCursor() System cursor position could not be adjusted for selection restore test.
- `RegionSelector_StyleSync`: SKIP   : TestRegionSelectorStyleSync::testInitializeForScreen_MagnifierStylePrewarmsCache() System cursor position could not be adjusted for magnifier prewarm test.
- `Video_SourceReaderMailbox`: SKIP   : tst_SourceReaderMailbox::nativeCallback() Windows COM callback test only
- `Detection_WindowDetectorQueryMode`: SKIP   : tst_WindowDetectorQueryMode::testLinuxX11TopLevelWindowDetectionFindsVisibleWindow() Linux X11 window detection is only tested on Linux.
- `CLI_PathPersistence`: SKIP   : tst_PathPersistence::registryRoundTrip() Windows registry fixture only
- `CLI_PathPersistence`: SKIP   : tst_PathPersistence::registryPreservesType(expandable) Windows registry fixture only
- `CLI_PathPersistence`: SKIP   : tst_PathPersistence::registryPreservesType(literal) Windows registry fixture only
- `IPC_SingleInstanceGuardConcurrentStart`: SKIP   : tst_SingleInstanceGuardRace::testConcurrentStart_ExactlyOnePrimary() Single-instance lock is unavailable in this environment. Preflight: state=0 exitStatus=0 exitCode=0 error="Unknown error" output=""
- `Settings_SettingsStorageLocation`: SKIP   : tst_SettingsStorageLocation::testLegacySettingsMigration() Legacy-to-platform settings migration applies only to Windows
- `Settings_SettingsStorageLocation`: SKIP   : tst_SettingsStorageLocation::testWindowsLegacyCleanupPreservesSiblingKeys() Windows registry cleanup applies only to Windows
- `Settings_SettingsStorageLocation`: SKIP   : tst_SettingsStorageLocation::testWindowsNamespaceStoresAreNotCleanupTargets() Windows namespace cleanup protection applies only to Windows
- `Settings_WindowsPrintScreenSettingsManager`: SKIP   : tst_WindowsPrintScreenSettingsManager::testDisableSnippingShortcut_WritesZeroToConfiguredRegistryKey() Windows Print Screen registry setting is only available on Windows.
- `Hotkey_HotkeyManager`: SKIP   : tst_HotkeyManager::testHasConflict_TreatsWindowsNativePrintAndPrintAsSameShortcut() Windows Print Screen canonicalization is only available on Windows.
- `Hotkey_HotkeyManager`: SKIP   : tst_HotkeyManager::testPrintSequence_UsesWindowsSnapshotVirtualKey() Windows virtual-key mapping is only available on Windows.
- `Hotkey_TypeHotkeyDialog`: SKIP   : tst_TypeHotkeyDialog::testWindowsPrintScreenNativeCapture_StoresPlatformSequence() Windows native Print Screen capture is only available on Windows.
- `RecordingManager_RecordingFileUtils`: SKIP   : TestRecordingFileUtils::testCommitFailurePreservesExistingDestination() The deterministic destination-lock failure uses Windows file sharing semantics.
- `RecordingManager_CaptureExclusion`: SKIP   : TestRecordingCaptureExclusion::sckFramesExcludeLateTooltip() Screen Recording permission unavailable; do not request TCC in tests.

## Unavailable acceptance resources

- Screen Recording permission is unavailable to the tool process; no request or reset was performed. The native capture-exclusion pixel test independently reports the same missing permission.
- The attached monitor is 3840×2160, not a 5K/6K capture source.
- Windows native OCR/COM/DXGI/Registry and old Windows exclusion behavior cannot be validated by this Mac.
- Linux X11 behavior, physical USB/Bluetooth disconnects, and multi-display UI need their stated environments.
- No dev-3 CI run existed when inspected. Workflow changes remain local until a separately authorized push.

The review tracker is authoritative for per-ID state; this document only records environment evidence.

## Final local checks

Canonical app build passed. Full CTest executed 183 suites; the single remaining crop failure was corrected and `ctest --test-dir build --rerun-failed --output-on-failure` passed. The final crop suite contains 67 passing cases. QML lint exited 0 with warnings; all 24 longshot translation catalogs passed `scripts/check-longshot-translations.py`. This does not upgrade the unavailable acceptance checks above.
