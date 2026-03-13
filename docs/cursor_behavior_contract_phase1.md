# Cursor Behavior Contract (Phase 1)

這份文件凍結第一階段重整前後必須維持的行為。新系統在 `shadow mode` 下只觀察、不接管，所有實際 cursor 仍以 legacy path 為準。

QML 端的 cursor style source 現在統一透過 `CursorTokens` singleton 表達，不再直接寫 `Qt.*Cursor` 常數。

目前這些 surface 已切到 `Authority` 預設：

- `RegionSelector`
- `ScreenCanvas`
- `PinWindow`
- `QmlRecordingRegionSelector`
- `QmlRecordingControlBar`
- `QmlCountdownOverlay`
- `RecordingPreviewBackend`
- `QmlDialog`
- `QmlBeautifyPanel`
- `QmlSettingsWindow`
- `QmlPinHistoryWindow`

如需 rollback，可用環境變數覆蓋：

- `SNAPTRAY_CURSOR_MODE_GROUP_REGIONSELECTOR=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_SCREENCANVAS=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_PINWINDOW=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_QMLRECORDINGREGIONSELECTOR=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_QMLRECORDINGCONTROLBAR=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_QMLCOUNTDOWNOVERLAY=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_RECORDINGPREVIEWBACKEND=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_QMLDIALOG=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_QMLBEAUTIFYPANEL=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_QMLSETTINGSWINDOW=shadow|legacy`
- `SNAPTRAY_CURSOR_MODE_GROUP_QMLPINHISTORYWINDOW=shadow|legacy`

## RegionSelector

| 場景 | owner | style | restore |
| --- | --- | --- | --- |
| pre-selection idle | host surface | `Crosshair` | 離開 toolbar/popup 後恢復 |
| hover selection toolbar | detached overlay | `Arrow` | 離開 overlay 後恢復 host cursor |
| toolbar drag | detached overlay | `ClosedHand` | drag 結束後恢復 host cursor |
| emoji popup visible | popup | `Arrow` | popup hide 後恢復 host cursor |
| mosaic tool active | host tool | `MosaicBrush(size)` | overlay/popup 後恢復相同 brush |
| eraser tool active | host tool | `EraserBrush(size)` | overlay/popup 後恢復相同 brush |

## ScreenCanvas

| 場景 | owner | style | restore |
| --- | --- | --- | --- |
| idle drawing surface | host tool | tool-specific | overlay/popup 後恢復 |
| hover floating toolbar / sub-toolbar | detached overlay | `Arrow` | 離開 overlay 後恢復 tool cursor |
| toolbar drag | detached overlay | `ClosedHand` | drag 結束後恢復 tool cursor |
| text editing | text editor | `TextBeam` | confirm mode / finish editing 後恢復 host cursor |

## PinWindow

| 場景 | owner | style | restore |
| --- | --- | --- | --- |
| annotation mode idle | host tool | tool-specific | overlay/popup 後恢復 |
| region layout resize | layout mode | resize cursor | hover/drag 結束後恢復 |
| region layout move | layout mode | `Move` | hover/drag 結束後恢復 |
| hover toolbar / sub-toolbar | detached overlay | `Arrow` | 離開 overlay 後恢復 annotation cursor |
| emoji popup visible | popup | `Arrow` | hide 後恢復 annotation cursor |

## Cross-platform expectations

- Windows 與 macOS 必須得到相同的 semantic resolved style。
- macOS 允許在 apply layer 額外呼叫 native cursor sync。
- Windows 不允許因為 native sync 為 no-op 而出現不同的 ownership 決策。

## Detached QML Windows

| 場景 | owner | style | restore |
| --- | --- | --- | --- |
| recording region selector | top-level QML surface | QML-resolved shape | hide/close 後 clear request |
| recording control bar | detached overlay | QML-resolved shape | hide/close 後 clear request |
| countdown overlay | blocking recording overlay | current window cursor | close 後 clear request |
| recording preview | utility preview window | QML-resolved shape | close 後 clear request |
| modal / result dialogs | popup | QML/dialog control shape | close 後 clear request |
| beautify panel | detached overlay | QML-resolved shape | hide/close 後 clear request |
| settings window | settings shell | QML control shape | hide/close 後 clear request |
| pin history window | utility window | QML-resolved shape | hide/close 後 clear request |
