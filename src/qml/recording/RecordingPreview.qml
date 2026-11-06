import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SnapTrayQml

/**
 * RecordingPreview.qml
 *
 * Video preview window for reviewing screen recordings before saving.
 * Uses token-driven colors/timings and SVG icons with dynamic tint.
 */
Item {
    id: root

    readonly property color accent: SemanticTokens.accentDefault
    readonly property color accentHover: SemanticTokens.accentHover
    readonly property color iconColor: SemanticTokens.iconColor
    readonly property color textPrimary: SemanticTokens.textPrimary
    readonly property color textSecondary: SemanticTokens.textSecondary
    readonly property color bgPanel: ComponentTokens.editorSurface
    readonly property color bgPanelHover: ComponentTokens.recordingPreviewPanelHover
    readonly property color bgPanelPressed: ComponentTokens.recordingPreviewPanelPressed
    readonly property color borderColor: ComponentTokens.editorBorder
    readonly property color trimOverlayColor: ComponentTokens.recordingPreviewTrimOverlay
    readonly property real disabledControlOpacity: 0.6
    readonly property real overlayChipHeight: 28
    readonly property real overlayChipButtonSize: 20


    property int selectedPart: -1
    readonly property var selectedPartData: selectedPart >= 0 && selectedPart < backend.timelineParts.length
        ? backend.timelineParts[selectedPart] : null
    property bool inspectorOpen: false
    readonly property bool wideEditor: width >= 980
    // What the inspector shows, in priority order: a draft crop, a selected
    // object, a whole-recording category, then the project overview.
    readonly property string inspectorKind: cropOverlay.editing ? "crop"
        : backend.selectedMask >= 0 ? "mask"
        : backend.selectedZoom >= 0 ? "zoom"
        : backend.selectedOverlay >= 0 ? (selectedOverlayData && selectedOverlayData.kind === 1 ? "image" : "text")
        : backend.selectedPointerHide >= 0 ? "pointerHide"
        : selectedPart >= 0 ? "clip"
        // A category whose panel is hidden (the format changed under it) is
        // not shown in the title either.
        : appearanceOpen && backend.appearanceAvailable ? "appearance"
        : pointerOptionsOpen && backend.cursorEffectsAvailable ? "pointer"
        : audioOpen && backend.audioMixAvailable ? "audio"
        : keysOpen && backend.keyOverlayAvailable ? "keys"
        : "project"
    readonly property var inspectorTitles: ({
        crop: qsTr("Crop"), mask: qsTr("Mask"), zoom: qsTr("Zoom segment"),
        text: qsTr("Text overlay"), image: qsTr("Image overlay"), pointerHide: qsTr("Hidden pointer"),
        clip: qsTr("Video clip"), appearance: qsTr("Canvas"), pointer: qsTr("Pointer"),
        audio: qsTr("Audio"), keys: qsTr("Keys"), project: qsTr("Project settings")
    })
    readonly property var inspectorIcons: ({
        crop: "crop", mask: "square-dashed", zoom: "zoom-in", text: "text", image: "image",
        pointerHide: "mouse-pointer", clip: "film", appearance: "frame", pointer: "mouse-pointer",
        audio: "volume-2", keys: "keyboard", project: "panel-right"
    })
    readonly property string inspectorTitle: inspectorTitles[inspectorKind]
    // What closing keeps, shown in the top bar.
    property string projectStatusText: backend.isProject ? qsTr("Project")
        : backend.keepsProject ? qsTr("Editable project") : qsTr("Unsaved recording")
    // Where the shown settings apply (#218: type and time range together).
    function rangeText(item) {
        if (!item) return ""
        if (item.start === 0 && item.end < 0) return qsTr("Whole recording")
        return qsTr("%1 – %2", "time range").arg(backend.formatPreciseTime(item.start))
                        .arg(item.end < 0 ? qsTr("the end") : backend.formatPreciseTime(item.end))
    }
    readonly property string inspectorSubtitle: {
        switch (inspectorKind) {
        case "mask": return rangeText(selectedMaskData)
        case "zoom": return rangeText(backend.zoomSegments[backend.selectedZoom])
        case "text": case "image": return rangeText(selectedOverlayData)
        case "pointerHide": return rangeText(selectedPointerHideData)
        case "clip": return rangeText(selectedPartData)
        case "crop": return qsTr("Applies to the whole recording")
        default: return qsTr("Whole recording")
        }
    }
    function iconUrl(name) { return "qrc:/icons/icons/" + name + ".svg" }

    function commitInspector() {
        if (backend.selectedZoom >= 0 && !root.commitPendingRange(zoomStartField, zoomEndField,
                (start, end) => backend.setZoomRange(backend.selectedZoom, start, end))) return false
        if (backend.selectedMask >= 0 && !maskPanel.wholeRecording
                && !root.commitPendingRange(maskStartField, maskEndField,
                (start, end) => backend.setMaskRange(backend.selectedMask, start, end))) return false
        if (backend.selectedOverlay >= 0 && !overlayPanel.wholeRecording
                && !root.commitPendingRange(overlayStartField, overlayEndField,
                (start, end) => backend.setOverlayRange(backend.selectedOverlay, start, end))) return false
        if (backend.selectedPointerHide >= 0 && !root.commitPendingRange(pointerHideStartField, pointerHideEndField,
                (start, end) => backend.setPointerHideRange(backend.selectedPointerHide, start, end))) return false
        return true
    }
    function clearSelection() {
        selectedPart = -1
        backend.selectedZoom = -1
        backend.selectedMask = -1
        backend.selectedOverlay = -1
        backend.selectedPointerHide = -1
    }
    function selectEffect(kind, index) {
        if (!commitInspector()) return false
        if (cropOverlay.editing && !applyCropWithPendingInput()) return false
        clearSelection()
        if (kind === "zoom") backend.selectedZoom = index
        if (kind === "mask") backend.selectedMask = index
        if (kind === "overlay") backend.selectedOverlay = index
        if (kind === "pointer") backend.selectedPointerHide = index
        inspectorOpen = index >= 0
        return true
    }
    // Adding an effect switches the inspector to it, so it follows the same
    // contract as selecting one: typed ranges are committed and a crop draft is
    // applied first; invalid input blocks the switch (PR #228 review).
    function prepareNewEffect() {
        if (!commitInspector()) return false
        if (cropOverlay.editing && !applyCropWithPendingInput()) return false
        return true
    }
    function selectPart(index) {
        if (!commitInspector()) return false
        if (cropOverlay.editing && !applyCropWithPendingInput()) return false
        clearSelection()
        selectedPart = index
        inspectorOpen = true
        return true
    }
    function prepareEffectInteraction(kind, index) {
        // Committing a range rebuilds Repeater delegates. Select through this
        // stable root, then let the replacement delegate handle the next drag.
        const pending = [zoomStartField, zoomEndField, maskStartField, maskEndField,
            overlayStartField, overlayEndField, pointerHideStartField, pointerHideEndField]
            .some(field => field.visible && field.hasPendingText())
        if (!commitInspector()) return false
        if (pending) { selectEffect(kind, index); return false }
        return true
    }
    function editEffectRange(kind, index, start, end) {
        if (!selectEffect(kind, index)) return
        videoPlayer.pause()
        if (kind === "mask") backend.setMaskRange(index, start, end)
        else if (kind === "zoom") backend.setZoomRange(index, start, end)
        else if (kind === "overlay") backend.setOverlayRange(index, start, end)
        else if (kind === "pointer") backend.setPointerHideRange(index, start, end)
    }
    // Whether any of [start, end) reaches the export: inside the trim and
    // in a kept (not cut) part. An end of -1 runs to the end.
    function rangeExported(start, end) {
        const from = Math.max(start, backend.trimStart)
        const to = Math.min(end < 0 ? backend.duration : end, backend.trimEnd)
        if (to <= from) return false
        const parts = backend.timelineParts
        if (parts.length === 0) return true
        for (const part of parts) {
            if (!part.deleted && part.start < to && from < part.end) return true
        }
        return false
    }
    function openSection(section) {
        if (!commitInspector()) return false
        if (cropOverlay.editing && !applyCropWithPendingInput()) return false
        clearSelection()
        pointerOptionsOpen = section === "pointer"
        appearanceOpen = section === "appearance"
        audioOpen = section === "audio"
        keysOpen = section === "keys"
        inspectorOpen = true
        return true
    }
    function openExport() {
        if (backend.isProcessing || backend.projectLoading || !commitInspector()) return
        for (const field of [trimStartField, trimEndField]) {
            if (field.visible && field.hasPendingText() && !field.commitText()) {
                field.forceActiveFocus()
                return
            }
        }
        if (cropOverlay.editing && !applyCropWithPendingInput()) return
        exportPanel.returnFocusItem = Window.activeFocusItem
        exportPanel.open()
    }

    property bool isScrubbing: false
    // Shows the unedited frame (no crop dimming) while on. Only meaningful
    // with a committed crop, and never while a crop is being edited.
    property bool comparingOriginal: false
    Connections {
        target: backend
        function onSelectedZoomChanged() { if (backend.selectedZoom >= 0) { root.selectedPart = -1; root.inspectorOpen = true } }
        function onSelectedMaskChanged() { if (backend.selectedMask >= 0) { root.selectedPart = -1; root.inspectorOpen = true } }
        function onSelectedOverlayChanged() { if (backend.selectedOverlay >= 0) { root.selectedPart = -1; root.inspectorOpen = true } }
        function onSelectedPointerHideChanged() { if (backend.selectedPointerHide >= 0) { root.selectedPart = -1; root.inspectorOpen = true } }
        function onTimelineChanged() { root.selectedPart = -1 }
        function onCropRectChanged() {
            if (!backend.hasCrop)
                root.comparingOriginal = false
        }
    }
    readonly property var aspectPresetLabels: [qsTr("Free"), qsTr("Original"), "16:9", "4:3", "1:1", "9:16"]
    // The "backend" context object, for items that have a property of that name.
    readonly property QtObject previewBackend: backend
    // Pointer effects (#160): size steps and smoothing levels, in backend order.
    readonly property var pointerScales: [1.0, 1.5, 2.0, 3.0]
    readonly property var pointerSmoothingLabels: [qsTr("Off"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
    // #186: choices in ms (0 = off) and blur levels in backend order.
    readonly property var pointerIdleChoices: [0, 1000, 2000, 5000]
    readonly property var pointerStillEndChoices: [0, 500, 1000, 2000]
    readonly property var pointerBlurLabels: [qsTr("Off"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
    // Project settings category currently shown in the inspector.
    property bool pointerOptionsOpen: false
    // Text and image overlays (#188), in OverlaySegment order.
    readonly property var selectedOverlayData: backend.selectedOverlay >= 0 && backend.selectedOverlay < backend.overlays.length
        ? backend.overlays[backend.selectedOverlay] : null
    readonly property var overlayColors: ["#ffffff", "#000000", "#ffd60a", "#ff453a", "#0a84ff"]
    readonly property var overlayColorNames: [qsTr("White"), qsTr("Black"), qsTr("Yellow"), qsTr("Red"), qsTr("Blue")]
    readonly property var overlayBackgroundLabels: [qsTr("None"), qsTr("Dark"), qsTr("Light")]
    readonly property var overlayAlignLabels: [qsTr("Left"), qsTr("Center"), qsTr("Right")]
    // Separate audio tracks (#189).
    property bool audioOpen: false
    // 6 dB steps over the whole range the mixer accepts
    // (AudioMixSettings::kMinGainDb..kMaxGainDb).
    readonly property var audioGainChoices: [-24, -18, -12, -6, 0, 6, 12]
    function gainLabel(db) {
        return (db > 0 ? "+" : "") + Number(db).toLocaleString(Qt.locale(), "g", 6) + " dB"
    }
    // Shortcut hints (#187), in KeyOverlaySettings order.
    property bool keysOpen: false
    readonly property var keyPositionLabels: [qsTr("Bottom"), qsTr("Bottom Left"), qsTr("Bottom Right"), qsTr("Top")]
    readonly property var keySizeLabels: [qsTr("Small"), qsTr("Medium"), qsTr("Large")]
    readonly property var keyDisplayChoices: [1000, 1500, 2500]
    // The frame around the export (#184), in RecordingAppearance order.
    property bool appearanceOpen: false
    readonly property var appearanceAmountLabels: [qsTr("None"), qsTr("Small"), qsTr("Medium"), qsTr("Large")]
    readonly property var appearanceRatioLabels: [qsTr("Auto"), "1:1", "16:9", "4:3", "9:16", "2:1"]
    // Shown in the preview: the frame, unless the crop is being edited or the
    // original is compared.
    readonly property bool appearanceShown: backend.appearanceActive && !cropOverlay.editing && !root.comparingOriginal
    readonly property rect appearanceCanvas: {
        void backend.appearanceEnabled; void backend.appearancePadding; void backend.appearanceAspectRatio
        void backend.cropRect; void backend.videoSize; void backend.selectedFormat
        return backend.appearanceCanvasInView(root.zoomTarget)
    }
    // MP4 export options (#190), in Mp4ExportOptions order.
    readonly property var mp4ResolutionLabels: [qsTr("Original"), "1080p", "720p",
                                                qsTr("Custom")]
    readonly property var mp4FrameRateChoices: [0, 60, 30, 15]
    readonly property var availableMp4FrameRates: mp4FrameRateChoices.filter(v => v === 0 || v < backend.mp4SourceFrameRate)
    readonly property var mp4QualityLabels: [qsTr("Default"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
    readonly property var selectedPointerHideData: backend.selectedPointerHide >= 0
        ? backend.pointerHiddenRanges[backend.selectedPointerHide] : null
    function secondsLabel(ms) {
        return Number(ms / 1000).toLocaleString(Qt.locale(), "g", 6)
    }
    // Zoom (#161): what the preview shows, the same viewport the export uses.
    // Identity while editing the crop or comparing with the original.
    readonly property rect zoomTarget: backend.hasCrop ? backend.cropRectInView(videoPlayer.contentRect)
                                                       : videoPlayer.contentRect
    readonly property rect zoomView: {
        // Re-evaluate when the zoom, crop or format changes, not only the position.
        void backend.zoomSegments; void backend.zoomEnabled; void backend.zoomReduceMotion
        void backend.cropRect; void backend.selectedFormat
        if (cropOverlay.editing || root.comparingOriginal || backend.selectedMask >= 0)
            return root.zoomTarget
        // Per frame, like the pointer overlay (#175 review): the reported
        // position would pan the zoom at 10 fps on some platforms.
        return backend.zoomViewportInView(videoPlayer.framePosition, videoPlayer.contentRect)
    }
    readonly property real zoomFactor: zoomView.width > 0 ? zoomTarget.width / zoomView.width : 1
    readonly property var zoomScales: [1.5, 2.0, 2.5, 3.0]
    // Masks (#162): style names in RedactionMask::Style order.
    readonly property var maskStyleLabels: [qsTr("Solid"), qsTr("Pixelate"), qsTr("Blur")]
    readonly property var selectedMaskData: backend.selectedMask >= 0 ? backend.masks[backend.selectedMask] : null
    readonly property int newMaskRangeMs: 3000
    // A point on the stage, through the zoom, in unzoomed stage coordinates.
    function unzoomPoint(x, y) {
        return Qt.point(zoomView.x + (x - zoomTarget.x) / zoomFactor, zoomView.y + (y - zoomTarget.y) / zoomFactor)
    }

    // A text field has keyboard focus: playback shortcuts must not steal its keys.
    readonly property bool textEntryActive: !!Window.activeFocusItem
                                            && Window.activeFocusItem.selectedText !== undefined
    // Legal (exported) crop of the current draft, in video pixels.
    readonly property rect draftVideoRect: cropOverlay.editing && cropOverlay.hasShownRect
                                           ? backend.legalCropForView(cropOverlay.draftRect, videoPlayer.contentRect)
                                           : Qt.rect(0, 0, 0, 0)
    property bool isDraggingTrimStart: false
    property bool isDraggingTrimEnd: false
    // The split marker being dragged, if any.
    property Item splitDragItem: null
    readonly property bool timelineDragActive: splitDragItem !== null || isDraggingTrimStart || isDraggingTrimEnd
    // Escape during a split or trim drag: nothing changes, and the release
    // that follows commits nothing.
    function cancelTimelineDrag() {
        if (splitDragItem) {
            splitDragItem.dragMs = -1
            splitDragItem = null
        }
        if (isDraggingTrimStart || isDraggingTrimEnd) scrubber.cancelTrimDrag()
    }
    // Escape cancels a drag in progress first, even in a focused field or
    // text box that would take it for itself; the field keeps its text.
    function cancelTimelineDragOnEscape(event) {
        if (!timelineDragActive) return false
        cancelTimelineDrag()
        event.accepted = true
        return true
    }
    // A held arrow key on a split: its first move was recorded as an undo
    // step and its repeats are folded into it. Kept here because each move
    // rebuilds the marker.
    property bool splitNudgeHeld: false

    readonly property var speedOptions: [0.25, 0.5, 0.75, 1.0, 1.5, 2.0]
    property int speedIndex: 3
    // Cuts and speed changes (#185): the part under the playhead and the
    // speeds a part can have, in TimelineEdit order.
    readonly property var partSpeeds: [0.5, 1.0, 1.5, 2.0, 4.0]
    readonly property int playheadPart: {
        void backend.timelineParts
        return backend.timelinePartAt(videoPlayer.position)
    }
    readonly property var playheadPartData: backend.timelineParts.length > root.playheadPart
        ? backend.timelineParts[root.playheadPart] : null
    // The clip that Cut and Delete act on: the selected one, as the inspector
    // shows; the one at the playhead only when no clip is selected.
    readonly property bool cutTargetsSelection: selectedPartData !== null
    readonly property int cutTargetPart: cutTargetsSelection ? selectedPart : playheadPart
    readonly property var cutTargetPartData: cutTargetsSelection ? selectedPartData : playheadPartData
    function toggleCutTarget() {
        const data = cutTargetPartData
        if (!backend.timelineEditsAvailable || data === null || backend.timelineParts.length <= 1)
            return false
        // A timeline change clears the clip selection; keep the clip just edited selected.
        const selected = selectedPart
        backend.setPartDeleted(cutTargetPart, !data.deleted)
        selectedPart = selected
        return true
    }
    function speedLabel(speed) {
        return Number(speed).toLocaleString(Qt.locale(), "g", 6) + "×"
    }
    // The preview plays the edit: the chosen preview speed times the part's.
    // What every platform player can play (IVideoPlayer::setPlaybackRate).
    readonly property real minPlayerRate: 0.25
    readonly property real maxPlayerRate: 2.0
    property real requestedPlaybackRate: 0
    function applyPlaybackRate() {
        // A 4x part at normal preview speed asks for 4x: the preview plays it
        // as fast as the player can (the export keeps 4x). Only a new request
        // is set: a player that adjusts the rate and seeks re-enters here
        // through positionChanged (PR #199 review).
        const wanted = root.speedOptions[root.speedIndex] * backend.playbackSpeedAt(videoPlayer.position)
        const rate = Math.max(root.minPlayerRate, Math.min(root.maxPlayerRate, wanted))
        if (Math.abs(root.requestedPlaybackRate - rate) < 0.0001)
            return
        root.requestedPlaybackRate = rate
        if (videoPlayer.playbackRate !== rate)
            videoPlayer.playbackRate = rate
    }
    property string activeTooltipText: ""
    property rect activeTooltipAnchor: Qt.rect(0, 0, 0, 0)
    property bool activeTooltipPreferAbove: true
    property var activeTooltipSourceItem: null

    // Crop editing needs a decoded frame on screen (contentRect is empty until one is shown and
    // again once it is cleared) and the video size the crop is mapped to.
    readonly property bool cropAvailable: backend.videoSize.width > 0 && backend.videoSize.height > 0
                                          && videoPlayer.contentRect.width > 0
                                          && videoPlayer.contentRect.height > 0
    onCropAvailableChanged: {
        if (!cropAvailable)
            cropOverlay.cancel()
    }

    // Final guarded export path; all entry points first open the export panel.
    // A visible crop draft is applied first so the export matches what the user sees.
    function saveWithCrop() {
        if (backend.isProcessing || backend.projectLoading)
            return
        // A time typed but not yet confirmed (Save does not take focus) is
        // applied first; an invalid one stops the save instead of exporting
        // the old range.
        for (const field of [trimStartField, trimEndField]) {
            if (field.visible && field.hasPendingText() && !field.commitText()) {
                field.forceActiveFocus()
                return
            }
        }
        if (backend.selectedZoom >= 0 && !root.commitPendingRange(zoomStartField, zoomEndField,
                (start, end) => backend.setZoomRange(backend.selectedZoom, start, end)))
            return
        if (backend.selectedMask >= 0 && !maskPanel.wholeRecording && !root.commitPendingRange(maskStartField, maskEndField,
                (start, end) => backend.setMaskRange(backend.selectedMask, start, end)))
            return
        if (backend.selectedOverlay >= 0 && !overlayPanel.wholeRecording && !root.commitPendingRange(overlayStartField, overlayEndField,
                (start, end) => backend.setOverlayRange(backend.selectedOverlay, start, end)))
            return
        if (backend.selectedFormat === 0 && mp4WidthField.visible && !mp4WidthField.commitText()) {
            mp4WidthField.forceActiveFocus()
            return
        }
        if (backend.selectedPointerHide >= 0 && !root.commitPendingRange(pointerHideStartField, pointerHideEndField,
                (start, end) => backend.setPointerHideRange(backend.selectedPointerHide, start, end)))
            return
        if (cropOverlay.editing && !root.applyCropWithPendingInput())
            return
        exportPanel.close()
        backend.save()
    }


    // Where an overlay is fully shown, past its fade-in.
    readonly property int overlayRevealMs: 400
    function showOverlay(index) {
        if (index < 0 || index >= backend.overlays.length)
            return
        const overlay = backend.overlays[index]
        const end = overlay.end < 0 ? timelineArea.dur : overlay.end
        videoPlayer.pause()
        videoPlayer.seek(Math.max(overlay.start, Math.min(end - 1, overlay.start + root.overlayRevealMs)))
    }

    // A start/end pair typed but not confirmed yet (Save does not take
    // focus), applied together so a range that needs both ends works. False,
    // with the field marked and focused, when it is unreadable or rejected.
    function commitPendingRange(startField, endField, apply) {
        if (!startField.edited && !endField.edited)
            return true
        const start = startField.edited ? backend.parseTime(startField.text) : startField.timeMs
        // An untouched open end ("until the end") keeps its -1 sentinel: the field
        // only shows it as the duration (PR #228 review).
        const keepsOpenEnd = !endField.edited && endField.openEnded
        const end = endField.edited ? backend.parseTime(endField.text) : keepsOpenEnd ? -1 : endField.timeMs
        // Unreadable text marks its own field; a rejected range marks what was typed.
        const unreadable = start < 0 || (end < 0 && !keepsOpenEnd)
        if (unreadable || !apply(start, end)) {
            startField.invalid = startField.edited && (!unreadable || start < 0)
            endField.invalid = endField.edited && (!unreadable || end < 0)
            const focusField = startField.invalid ? startField : endField
            focusField.forceActiveFocus()
            return false
        }
        for (const field of [startField, endField]) {
            field.edited = false
            field.invalid = false
            field.text = backend.formatPreciseTime(field.timeMs)
        }
        return true
    }

    // Apply Crop and Save do not take focus: typed X/Y/W/H not yet confirmed
    // is applied first (#167 review); invalid input keeps the editor open.
    function applyCropWithPendingInput() {
        for (let i = 0; i < cropFieldRepeater.count; ++i) {
            const field = cropFieldRepeater.itemAt(i).field
            if (field.hasPendingInput() && !field.commitText()) {
                field.forceActiveFocus()
                return false
            }
        }
        cropOverlay.apply()
        return true
    }

    function undoEdit() {
        if (!root.commitInspector()) return
        root.clearSelection()
        cropOverlay.cancel()
        backend.undo()
    }
    function redoEdit() {
        if (!root.commitInspector()) return
        root.clearSelection()
        cropOverlay.cancel()
        backend.redo()
    }
    function chooseAspect(index) {
        backend.cropAspectIndex = index
        if (cropOverlay.editing && backend.cropAspectRatio > 0)
            cropOverlay.draftRect = backend.fitViewRectToAspect(cropOverlay.draftRect, videoPlayer.contentRect)
    }
    // Exact video-pixel input for the draft.
    function setDraftFromFields(x, y, w, h) {
        if (!cropOverlay.editing)
            return
        cropOverlay.draftRect = backend.legalViewRectForVideo(x, y, w, h, videoPlayer.contentRect)
    }
    // Arrow keys move the crop draft by 2 video pixels (10 with Shift).
    function nudgeDraft(dxVideo, dyVideo) {
        const r = root.draftVideoRect
        if (r.width <= 0)
            return
        // Moving never resizes: at an edge the crop stops instead of shrinking.
        const x = Math.max(0, Math.min(backend.videoSize.width - r.width, r.x + dxVideo))
        const y = Math.max(0, Math.min(backend.videoSize.height - r.height, r.y + dyVideo))
        setDraftFromFields(x, y, r.width, r.height)
    }

    // The window under the resting pointer follows the playhead too.
    function refreshWindowHover() {
        if (cropOverlay.pressed)
            return
        if (!cropOverlay.editing || !cropOverlay.hovering || !backend.hasWindowTimeline) {
            cropOverlay.hoverRect = Qt.rect(0, 0, 0, 0)
            cropOverlay.hoverLabel = ""
            return
        }
        const p = cropOverlay.hoverPoint
        cropOverlay.hoverRect = backend.windowRectInViewAt(p, videoPlayer.contentRect, videoPlayer.position)
        cropOverlay.hoverLabel = cropOverlay.isNonEmpty(cropOverlay.hoverRect)
                ? backend.windowAppAt(p, videoPlayer.contentRect, videoPlayer.position) : ""
    }

    function showButtonTooltip(text, item, preferAbove) {
        if (!text || !item || backend.isProcessing) {
            hideButtonTooltip(item)
            return
        }

        var mapped = item.mapToItem(root, 0, 0)
        activeTooltipText = text
        activeTooltipAnchor = Qt.rect(mapped.x, mapped.y, item.width, item.height)
        activeTooltipPreferAbove = preferAbove === undefined ? true : preferAbove
        activeTooltipSourceItem = item
    }

    function hideButtonTooltip(item) {
        if (item !== undefined && item !== null && activeTooltipSourceItem !== item)
            return

        activeTooltipText = ""
        activeTooltipSourceItem = null
    }

    Rectangle {
        anchors.fill: parent
        color: ComponentTokens.editorCanvas
    }

    RecordingTooltip {
        id: hoverTooltip
        tooltipText: root.activeTooltipText
        themeGlassBg: ComponentTokens.tooltipBackground
        themeGlassBgTop: ComponentTokens.tooltipBackgroundTop
        themeHighlight: ComponentTokens.tooltipHighlight
        themeBorder: ComponentTokens.tooltipBorder
        themeText: ComponentTokens.tooltipText
        themeCornerRadius: ComponentTokens.tooltipRadius
        visible: root.activeTooltipText !== "" && !backend.isProcessing
        z: 20

        x: {
            var desiredX = root.activeTooltipAnchor.x + (root.activeTooltipAnchor.width - width) / 2
            var maxX = Math.max(6, root.width - width - 6)
            return Math.max(6, Math.min(desiredX, maxX))
        }

        y: {
            var maxY = Math.max(6, root.height - height - 6)
            var aboveY = root.activeTooltipAnchor.y - height - 6
            var belowY = root.activeTooltipAnchor.y + root.activeTooltipAnchor.height + 6

            if (root.activeTooltipPreferAbove) {
                if (aboveY >= 6)
                    return aboveY
                return Math.min(maxY, belowY)
            }

            if (belowY <= maxY)
                return belowY
            return Math.max(6, aboveY)
        }
    }

    RecordingEditorShell {
        id: editorShell
        accessibilityBlocked: exportPanel.visible
        anchors.fill: parent
        // Behind the modal export panel the editor cannot be used; disabling it
        // keeps screen readers from operating it there too (#248). `opened`
        // turns false when closing starts, so focus can return on close.
        enabled: !backend.isProcessing && !backend.projectLoading && !exportPanel.opened
        inspectorVisible: root.wideEditor || root.inspectorOpen
        inspectorOverlay: !root.wideEditor
        inspectorTitle: root.inspectorTitle
        inspectorIcon: root.iconUrl(root.inspectorIcons[root.inspectorKind])
        inspectorSubtitle: root.inspectorSubtitle
        showProjectLink: root.inspectorKind !== "project" && root.inspectorKind !== "crop"
        // An overlaid inspector covers the preview only: playback and timeline stay usable.
        overlayBottomInset: editorWorkspace.height - stage.height
        onProjectRequested: root.openSection("")
        onInspectorCloseRequested: {
            if (root.commitInspector()) root.inspectorOpen = false
        }
    }

    // Project identity, history and the primary Export action (#214).
    RowLayout {
        parent: editorShell.headerContent
        anchors.fill: parent
        spacing: SemanticTokens.spacing8
        SvgIcon {
            source: root.iconUrl("film")
            iconSize: 18
            color: root.textSecondary
        }
        Text {
            objectName: "previewTitle"
            text: {
                const path = String(backend.videoPath)
                const name = path.substring(Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\")) + 1)
                return name.length > 0 ? name : qsTr("Recording")
            }
            color: root.textPrimary
            font.pixelSize: SemanticTokens.fontSizeBody
            font.weight: SemanticTokens.fontWeightSemiBold
            elide: Text.ElideMiddle
            // Name and status shrink first; history, Export and discard keep their room.
            Layout.preferredWidth: Math.min(implicitWidth, root.width * (root.wideEditor ? 0.4 : 0.25))
            Layout.minimumWidth: 40
        }
        // Says what closing keeps: an editable project or only this recording.
        Rectangle {
            id: statusChip
            objectName: "previewStatusChip"
            readonly property real maximumWidth: root.wideEditor ? 200 : 120
            Layout.preferredHeight: 22
            Layout.preferredWidth: Math.min(statusText.implicitWidth + 16, maximumWidth)
            Layout.minimumWidth: 0
            Layout.maximumWidth: maximumWidth
            visible: Layout.preferredWidth > 0
            clip: true
            radius: 11
            color: backend.keepsProject || backend.isProject ? ComponentTokens.editorSelection : ComponentTokens.editorControl
            ToolTip.visible: statusHover.hovered && statusText.truncated
            ToolTip.text: root.projectStatusText
            HoverHandler { id: statusHover }
            Text {
                id: statusText
                anchors.verticalCenter: parent.verticalCenter
                x: 8
                width: parent.width - 16
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: root.projectStatusText
                color: root.textPrimary
                font.pixelSize: SemanticTokens.fontSizeCaption
                font.weight: SemanticTokens.fontWeightMedium
            }
        }
        Item { Layout.fillWidth: true }
        IconButton {
            objectName: "previewUndoButton"
            Layout.minimumWidth: ComponentTokens.recordingPreviewControlButtonSize
            iconSource: root.iconUrl("undo")
            enabled: backend.canUndo
            opacity: enabled ? 1.0 : root.disabledControlOpacity
            tooltipPreferredAbove: false
            tooltipText: qsTr("Undo Edit (Ctrl+Z)")
            Accessible.name: qsTr("Undo Edit")
            onClicked: root.undoEdit()
        }
        IconButton {
            objectName: "previewRedoButton"
            Layout.minimumWidth: ComponentTokens.recordingPreviewControlButtonSize
            iconSource: root.iconUrl("redo")
            enabled: backend.canRedo
            opacity: enabled ? 1.0 : root.disabledControlOpacity
            tooltipPreferredAbove: false
            tooltipText: qsTr("Redo Edit (Ctrl+Shift+Z)")
            Accessible.name: qsTr("Redo Edit")
            onClicked: root.redoEdit()
        }
        // Narrow windows: the inspector is a sheet opened from here.
        IconButton {
            objectName: "previewInspectorToggle"
            Layout.minimumWidth: ComponentTokens.recordingPreviewControlButtonSize
            visible: !root.wideEditor
            iconSource: root.iconUrl("panel-right")
            highlighted: root.inspectorOpen
            tooltipPreferredAbove: false
            tooltipText: root.inspectorOpen ? qsTr("Hide settings") : qsTr("Show settings")
            onClicked: {
                if (!root.inspectorOpen) root.inspectorOpen = true
                else if (root.commitInspector()) root.inspectorOpen = false
            }
        }
        SegmentButton {
            objectName: "previewExportOptions"
            Layout.minimumWidth: implicitWidth
            text: qsTr("Export…")
            iconSource: root.iconUrl("upload")
            primary: true
            tooltipPreferredAbove: false
            tooltipText: qsTr("Choose a format and export (Ctrl+S)")
            onClicked: root.openExport()
        }
        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 20
            color: root.borderColor
        }
        IconButton {
            iconSource: root.iconUrl("trash-2")
            destructive: true
            objectName: "previewDiscardButton"
            Layout.minimumWidth: ComponentTokens.recordingPreviewControlButtonSize
            tooltipPreferredAbove: false
            tooltipText: backend.isProject ? qsTr("Close Without Saving (Esc)") : qsTr("Discard Recording (Esc)")
            onClicked: backend.requestDiscard()
        }
    }

    ColumnLayout {
        id: editorWorkspace
        parent: editorShell.workspace
        anchors.fill: parent
        spacing: 0
        enabled: !backend.isProcessing

        Item {
            id: stage
            objectName: "previewStage"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // A zoomed picture never spills over the controls. Only while
            // zoomed: crop handles at the edges must stay visible when editing.
            // With a frame around the export the zoom clips itself (ZoomClip)
            // and the frame may reach past the stage before it is fitted.
            clip: root.zoomFactor > 1.001 && !root.appearanceShown

            // The framed export (#184) is bigger than the picture: the whole
            // stage is scaled to fit it and centred, so every overlay and
            // pointer mapping stays as it is.
            readonly property real fitScale: root.appearanceShown && root.appearanceCanvas.width > 0
                ? Math.min(1, (width - 2 * SemanticTokens.spacing8) / root.appearanceCanvas.width,
                           (height - 2 * SemanticTokens.spacing8) / root.appearanceCanvas.height)
                : 1
            transform: [
                Translate {
                    x: root.appearanceShown ? -(root.appearanceCanvas.x + root.appearanceCanvas.width / 2) : 0
                    y: root.appearanceShown ? -(root.appearanceCanvas.y + root.appearanceCanvas.height / 2) : 0
                },
                Scale { xScale: stage.fitScale; yScale: stage.fitScale },
                Translate {
                    x: root.appearanceShown ? stage.width / 2 : 0
                    y: root.appearanceShown ? stage.height / 2 : 0
                }
            ]

            // The zoomed picture stays inside the frame the export shows: the
            // video and pointer are drawn whole and transformed, so without
            // this they would spill into the letterbox margins (#176 review).
            // Children sit back at stage coordinates.
            component ZoomClip: Item {
                x: root.zoomTarget.x
                y: root.zoomTarget.y
                width: root.zoomTarget.width
                height: root.zoomTarget.height
                // Framed (#184): only the cropped picture is in the export,
                // so nothing outside it shows around the frame (PR #198 review).
                clip: root.zoomFactor > 1.001 || root.appearanceShown
            }

            ZoomClip {
                id: zoomClip
                objectName: "previewZoomClip"

                VideoPlaybackItem {
                    id: videoPlayer
                    objectName: "previewVideoPlayer"
                    // The letterbox matches the editor canvas instead of a black slab.
                    backgroundColor: ComponentTokens.editorCanvas
                    x: -zoomClip.x
                    y: -zoomClip.y
                    width: zoomClip.parent.width
                    height: zoomClip.parent.height
                    source: backend.videoPath
                    transform: [
                        Translate { x: -root.zoomView.x; y: -root.zoomView.y },
                        Scale { xScale: root.zoomFactor; yScale: root.zoomFactor },
                        Translate { x: root.zoomTarget.x; y: root.zoomTarget.y }
                    ]

                    onVideoLoaded: backend.updateVideoSize(videoPlayer.videoSize)
                    onDurationChanged: function(durationMs) { backend.updateDuration(durationMs) }
                    onPositionChanged: function(positionMs) {
                        backend.updatePosition(positionMs)
                        root.refreshWindowHover()
                        // Cut-out parts are skipped while playing, and each
                        // part plays at its own speed (#185).
                        if (videoPlayer.playing && backend.hasTimelineEdits && !root.isScrubbing) {
                            const target = backend.playbackTarget(positionMs)
                            if (target < 0) {
                                // Past the last kept part: start over, or stop
                                // when every part is cut (seeking would land
                                // back here, forever; PR #199 review).
                                const restart = backend.playbackTarget(backend.trimStart)
                                if (restart < 0)
                                    videoPlayer.pause()
                                else
                                    videoPlayer.seek(restart)
                            } else if (target !== positionMs) {
                                videoPlayer.seek(target)
                            }
                        }
                        root.applyPlaybackRate()
                    }
                    onStateChanged: backend.updatePlayingState(videoPlayer.playing)
                    onErrorOccurred: function(message) { backend.reportPlaybackError(message) }
                    Component.onCompleted: {
                        setLooping(true)
                        play()
                    }
                }
            }

            // The pointer and click effects the export will draw, drawn by
            // the same renderer; hidden while comparing with the original.
            ZoomClip {
                id: pointerClip
                objectName: "previewPointerClip"
                z: 3

                RecordingCursorOverlay {
                    objectName: "previewCursorOverlay"
                    x: -pointerClip.x
                    y: -pointerClip.y
                    width: pointerClip.parent.width
                    height: pointerClip.parent.height
                    visible: !root.comparingOriginal
                    backend: root.previewBackend
                    // Each frame's own time: the player's position is reported
                    // only every 100 ms on some platforms (#175 review).
                    position: videoPlayer.framePosition
                    contentRect: videoPlayer.contentRect
                    transform: [
                        Translate { x: -root.zoomView.x; y: -root.zoomView.y },
                        Scale { xScale: root.zoomFactor; yScale: root.zoomFactor },
                        Translate { x: root.zoomTarget.x; y: root.zoomTarget.y }
                    ]
                }
            }

            MouseArea {
                objectName: "previewVideoClickArea"
                anchors.fill: parent
                // Video clicks belong to the crop editor while editing; Space and the play button still work.
                enabled: !cropOverlay.editing
                cursorShape: backend.selectedZoom >= 0 ? CursorTokens.pickPoint : CursorTokens.clickable
                // The text, image or mask shown under a point, topmost first (#218).
                function objectAt(x, y) {
                    // Only the exported picture shows them (not a cropped-away edge).
                    const t = root.zoomTarget
                    if (x < t.x || x >= t.x + t.width || y < t.y || y >= t.y + t.height) return null
                    // The shown frame's time, as the overlays and masks are drawn by;
                    // the player's position is reported less often.
                    const now = videoPlayer.framePosition
                    const showing = function(item) {
                        return item.start <= now && (item.end < 0 || now < item.end)
                    }
                    const inside = function(r, px, py) {
                        return px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height
                    }
                    // Overlays sit on the exported picture, never zoomed; hidden while comparing.
                    const overlays = root.comparingOriginal ? [] : backend.overlays
                    for (let i = overlays.length - 1; i >= 0; --i) {
                        if (showing(overlays[i]) && inside(backend.overlayRectInView(i, t), x, y))
                            return {kind: "overlay", index: i}
                    }
                    // Masks are burned into the video, which a zoom scales: map the click back.
                    const p = root.unzoomPoint(x, y)
                    const masks = backend.masks
                    for (let j = masks.length - 1; j >= 0; --j) {
                        if (showing(masks[j]) && inside(backend.maskRectInView(j, videoPlayer.contentRect), p.x, p.y))
                            return {kind: "mask", index: j}
                    }
                    return null
                }
                onClicked: function(mouse) {
                    // With a zoom selected, a click moves its centre there.
                    if (backend.selectedZoom >= 0) {
                        const p = root.unzoomPoint(mouse.x, mouse.y)
                        backend.setZoomFocusFromView(backend.selectedZoom, p, videoPlayer.contentRect)
                        return
                    }
                    // A click on a text, image or mask selects it, like its timeline block.
                    const hit = objectAt(mouse.x, mouse.y)
                    if (hit) {
                        videoPlayer.pause()
                        // The keyboard follows the selection, so Delete and the arrows
                        // act on it, not on a split marker that had the focus. Input
                        // that blocks the switch keeps its field focused.
                        if (root.selectEffect(hit.kind, hit.index))
                            root.forceActiveFocus(Qt.MouseFocusReason)
                        return
                    }
                    // While a mask is selected its editor owns the video.
                    if (backend.selectedMask >= 0) return
                    videoPlayer.togglePlayPause()
                }
            }

            // The selected mask: drag it to move, drag the corner to resize.
            // Shortcut hints (#187) on the exported picture, not zoomed with it.
            RecordingKeyOverlay {
                objectName: "previewKeyOverlay"
                anchors.fill: parent
                z: 4.2
                visible: !root.comparingOriginal && !cropOverlay.editing
                backend: root.previewBackend
                position: videoPlayer.framePosition
                contentRect: root.zoomTarget
            }

            // Text and image overlays (#188) on the exported picture.
            RecordingOverlayItem {
                objectName: "previewOverlayItem"
                anchors.fill: parent
                z: 4.3
                visible: !root.comparingOriginal && !cropOverlay.editing
                backend: root.previewBackend
                position: videoPlayer.framePosition
                contentRect: root.zoomTarget
            }

            // Moving and resizing the selected overlay, on the exported picture.
            Item {
                id: overlayEditor
                objectName: "previewOverlayEditor"
                anchors.fill: parent
                z: 5
                visible: backend.selectedOverlay >= 0 && !cropOverlay.editing
                property rect draft: Qt.rect(0, 0, 0, 0)
                property bool dragging: false
                readonly property rect shown: {
                    void backend.overlays
                    return dragging ? draft : backend.overlayRectInView(backend.selectedOverlay, root.zoomTarget)
                }
                function commit() {
                    dragging = false
                    backend.setOverlayRectFromView(backend.selectedOverlay, draft, root.zoomTarget)
                }

                Rectangle {
                    x: overlayEditor.shown.x
                    y: overlayEditor.shown.y
                    width: overlayEditor.shown.width
                    height: overlayEditor.shown.height
                    color: "transparent"
                    border.width: 2
                    border.color: root.accent
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Selected overlay")

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: CursorTokens.moveItem
                        property point origin
                        onPressed: function(mouse) {
                            origin = Qt.point(mouse.x, mouse.y)
                            overlayEditor.draft = overlayEditor.shown
                            overlayEditor.dragging = true
                        }
                        onPositionChanged: function(mouse) {
                            const c = root.zoomTarget
                            const d = overlayEditor.draft
                            const nx = Math.max(c.x, Math.min(c.x + c.width - Math.min(d.width, c.width), d.x + mouse.x - origin.x))
                            const ny = Math.max(c.y, Math.min(c.y + c.height - Math.min(d.height, c.height), d.y + mouse.y - origin.y))
                            overlayEditor.draft = Qt.rect(nx, ny, d.width, d.height)
                        }
                        onReleased: overlayEditor.commit()
                        onCanceled: overlayEditor.commit()
                    }

                    Rectangle {
                        objectName: "previewOverlayResizeHandle"
                        width: 12
                        height: 12
                        radius: 2
                        x: parent.width - width / 2
                        y: parent.height - height / 2
                        color: root.accent
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: CursorTokens.resizeDiagonal
                            onPressed: {
                                overlayEditor.draft = overlayEditor.shown
                                overlayEditor.dragging = true
                            }
                            onPositionChanged: function(mouse) {
                                // Keeps the shape: the width sets the scale.
                                const p = mapToItem(overlayEditor, mouse.x, mouse.y)
                                const d = overlayEditor.shown
                                const w = Math.max(8, p.x - d.x)
                                overlayEditor.draft = Qt.rect(d.x, d.y, w, d.height * w / Math.max(1, d.width))
                            }
                            onReleased: overlayEditor.commit()
                            onCanceled: overlayEditor.commit()
                        }
                    }
                }
            }

            // The frame around the export (#184), drawn by the exports' geometry.
            RecordingAppearanceOverlay {
                objectName: "previewAppearanceOverlay"
                z: 4.5
                visible: root.appearanceShown
                x: root.appearanceCanvas.x
                y: root.appearanceCanvas.y
                width: root.appearanceCanvas.width
                height: root.appearanceCanvas.height
                backend: root.previewBackend
                contentRect: Qt.rect(root.zoomTarget.x - x, root.zoomTarget.y - y,
                                     root.zoomTarget.width, root.zoomTarget.height)
            }

            Item {
                id: maskEditor
                objectName: "previewMaskEditor"
                anchors.fill: parent
                z: 5
                visible: backend.selectedMask >= 0 && !cropOverlay.editing
                property rect draft: Qt.rect(0, 0, 0, 0)
                property bool dragging: false
                readonly property rect shown: {
                    void backend.masks
                    return dragging ? draft : backend.maskRectInView(backend.selectedMask, videoPlayer.contentRect)
                }
                function commit() {
                    dragging = false
                    backend.setMaskRectFromView(backend.selectedMask, draft, videoPlayer.contentRect)
                }

                Rectangle {
                    x: maskEditor.shown.x
                    y: maskEditor.shown.y
                    width: maskEditor.shown.width
                    height: maskEditor.shown.height
                    color: "transparent"
                    border.width: 2
                    border.color: root.accent
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Selected mask")

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: CursorTokens.moveItem
                        property point origin
                        onPressed: function(mouse) {
                            origin = Qt.point(mouse.x, mouse.y)
                            maskEditor.draft = maskEditor.shown
                            maskEditor.dragging = true
                        }
                        onPositionChanged: function(mouse) {
                            const c = videoPlayer.contentRect
                            const d = maskEditor.draft
                            const nx = Math.max(c.x, Math.min(c.x + c.width - d.width, d.x + mouse.x - origin.x))
                            const ny = Math.max(c.y, Math.min(c.y + c.height - d.height, d.y + mouse.y - origin.y))
                            maskEditor.draft = Qt.rect(nx, ny, d.width, d.height)
                        }
                        onReleased: maskEditor.commit()
                        onCanceled: maskEditor.commit()
                    }

                    Rectangle {
                        objectName: "previewMaskResizeHandle"
                        width: 12
                        height: 12
                        radius: 2
                        x: parent.width - width / 2
                        y: parent.height - height / 2
                        color: root.accent
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: CursorTokens.resizeDiagonal
                            onPressed: {
                                maskEditor.draft = maskEditor.shown
                                maskEditor.dragging = true
                            }
                            onPositionChanged: function(mouse) {
                                const p = mapToItem(maskEditor, mouse.x, mouse.y)
                                const c = videoPlayer.contentRect
                                const d = maskEditor.draft
                                const w = Math.max(8, Math.min(c.x + c.width, p.x) - d.x)
                                const h = Math.max(8, Math.min(c.y + c.height, p.y) - d.y)
                                maskEditor.draft = Qt.rect(d.x, d.y, w, h)
                            }
                            onReleased: maskEditor.commit()
                            onCanceled: maskEditor.commit()
                        }
                    }
                }
            }

            RecordingCropOverlay {
                id: cropOverlay
                objectName: "previewCropOverlay"
                anchors.fill: parent
                z: 4
                visible: root.cropAvailable && !root.comparingOriginal
                // The framed export shows only the crop: no dimmed outside.
                dimOutside: !root.appearanceShown
                interactive: !backend.isProcessing
                aspectRatio: backend.cropAspectRatio
                legalSizeForDraft: function(r) {
                    const legal = backend.legalCropForView(r, videoPlayer.contentRect)
                    return Qt.size(legal.width, legal.height)
                }
                contentRect: videoPlayer.contentRect
                videoSize: backend.videoSize
                minVideoSide: backend.minCropSide
                accentColor: root.accent
                // backend.cropRect is read so the binding re-evaluates when the crop changes.
                committedRect: {
                    backend.cropRect
                    return backend.hasCrop ? backend.cropRectInView(videoPlayer.contentRect) : Qt.rect(0, 0, 0, 0)
                }
                onHoverChanged: root.refreshWindowHover()
                onApplyRequested: function(viewRect) {
                    if (!backend.isProcessing)
                        backend.setCropFromView(viewRect, videoPlayer.contentRect)
                }
            }

        }

        // Playback sits directly under the picture it controls (#214).
        Rectangle {
            id: playbackBar
            objectName: "previewPlaybackBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: root.bgPanel

            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: root.borderColor }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.borderColor }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: SemanticTokens.spacing12
                anchors.rightMargin: SemanticTokens.spacing12
                spacing: SemanticTokens.spacing8

                IconButton {
                    objectName: "previewPlayButton"
                    primary: true
                    iconSource: videoPlayer.playing ? root.iconUrl("pause") : root.iconUrl("play")
                    tooltipText: videoPlayer.playing ? qsTr("Pause Preview (Space)") : qsTr("Play Preview (Space)")
                    onClicked: videoPlayer.togglePlayPause()
                }

                Text {
                    objectName: "previewTimecode"
                    textFormat: Text.StyledText
                    text: backend.formatPreciseTime(videoPlayer.position)
                          + "<font color=\"" + root.textSecondary + "\"> / " + backend.formatPreciseTime(videoPlayer.duration) + "</font>"
                    elide: Text.ElideRight
                    Layout.leftMargin: SemanticTokens.spacing4
                    font.pixelSize: SemanticTokens.fontSizeSmall
                    font.family: SemanticTokens.fontFamily
                    font.weight: SemanticTokens.fontWeightMedium
                    font.features: { "tnum": 1 }
                    color: root.textPrimary
                }

                // The timecode above is source time; once something is trimmed,
                // cut or sped up, this says where the playhead lands in the file.
                Text {
                    objectName: "previewOutputTime"
                    visible: backend.hasTrim || backend.hasTimelineEdits
                    // Reads outputDuration so it follows every trim/cut/speed edit.
                    readonly property real outputMs: backend.outputDuration >= 0
                        ? backend.outputTimeAt(videoPlayer.position) : -1
                    text: outputMs < 0
                        ? qsTr("Not exported · export %1").arg(backend.formatPreciseTime(backend.outputDuration))
                        : qsTr("Export %1 / %2").arg(backend.formatPreciseTime(outputMs))
                            .arg(backend.formatPreciseTime(backend.outputDuration))
                    Accessible.name: outputMs < 0
                        ? qsTr("The playhead is in a part that is not exported. The export is %1 long.")
                            .arg(backend.formatPreciseTime(backend.outputDuration))
                        : qsTr("Exported time %1 of %2").arg(backend.formatPreciseTime(outputMs))
                            .arg(backend.formatPreciseTime(backend.outputDuration))
                    elide: Text.ElideRight
                    Layout.leftMargin: SemanticTokens.spacing4
                    Layout.maximumWidth: 220
                    Layout.minimumWidth: 0 // gives way first in a narrow window
                    font.pixelSize: SemanticTokens.fontSizeSmall
                    font.family: SemanticTokens.fontFamily
                    font.features: { "tnum": 1 }
                    color: root.textSecondary
                }

                Item { Layout.fillWidth: true }

                IconButton {
                    objectName: "previewMuteButton"
                    visible: videoPlayer.audioPlaybackSupported
                    iconSource: videoPlayer.muted ? root.iconUrl("volume-x") : root.iconUrl("volume-2")
                    tooltipText: videoPlayer.muted ? qsTr("Unmute Preview (M)") : qsTr("Mute Preview (M)")
                    onClicked: videoPlayer.muted = !videoPlayer.muted
                }

                IconButton {
                    objectName: "previewCropButton"
                    iconSource: root.iconUrl("crop")
                    enabled: root.cropAvailable && !backend.isProcessing
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    highlighted: backend.hasCrop || cropOverlay.editing
                    tooltipText: cropOverlay.editing ? qsTr("Apply Crop (Enter) · Arrow keys move it by 2 px (Shift: 10 px)")
                                                     : backend.hasCrop ? qsTr("Edit Crop") : qsTr("Crop Recording")
                    // Not the long editing tooltip: its first part, without the shortcut.
                    accessibleName: (cropOverlay.editing ? qsTr("Apply Crop (Enter)") : tooltipText)
                                    .replace(/\s*\([^()]*\)$/, "")
                    onClicked: {
                        root.comparingOriginal = false
                        if (cropOverlay.editing)
                            root.applyCropWithPendingInput()
                        else {
                            if (!root.commitInspector()) return
                            root.clearSelection()
                            root.inspectorOpen = true
                            cropOverlay.beginEditing()
                        }
                    }
                }

                IconButton {
                    objectName: "previewTrimButton"
                    iconSource: root.iconUrl("arrow-both")
                    highlighted: backend.hasTrim
                    tooltipText: backend.hasTrim ? qsTr("Clear Trim Selection") : qsTr("Trim Recording")
                    onClicked: backend.toggleTrim()
                }

                IconButton {
                    objectName: "previewCompareButton"
                    iconSource: root.iconUrl("compare")
                    Accessible.name: qsTr("Compare with Original")
                    enabled: backend.hasCrop
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    highlighted: root.comparingOriginal
                    // The shortcut goes in through %1: lupdate drops a "\\" inside qsTr().
                    tooltipText: qsTr("Compare with Original (%1)").arg("\\")
                    onClicked: root.comparingOriginal = !root.comparingOriginal
                }

                IconButton {
                    objectName: "previewResetButton"
                    iconSource: root.iconUrl("reset")
                    Accessible.name: qsTr("Reset Crop and Trim")
                    enabled: backend.hasEdits
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    tooltipText: qsTr("Reset Crop and Trim")
                    onClicked: {
                        cropOverlay.cancel()
                        backend.resetEdits()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 20
                    color: root.borderColor
                }

                EditorChoice {
                    objectName: "previewPlaybackSpeed"
                    // Room for the label and a usable control, whatever the translation.
                    Layout.preferredWidth: implicitWidth
                    inlineLabel: true
                    maximumInlineLabelWidth: root.wideEditor ? 220 : 140
                    label: qsTr("Preview speed")
                    tooltipText: qsTr("Playback speed of this preview only; the export is not changed")
                    choices: root.speedOptions.map(v => root.speedLabel(v))
                    currentIndex: root.speedIndex
                    onActivated: function(index) { root.speedIndex = index; root.applyPlaybackRate() }
                }
            }
        }

        // Timeline actions only: split, cut and effect creation (#219).
        Flow {
            id: timelineCommands
            objectName: "previewTimelineCommands"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.ceil(implicitHeight)
            Layout.leftMargin: SemanticTokens.spacing16
            Layout.rightMargin: SemanticTokens.spacing16
            Layout.topMargin: SemanticTokens.spacing8
            Layout.bottomMargin: SemanticTokens.spacing4
            spacing: 6
            SegmentButton {
                objectName: "previewSplit"
                enabled: backend.timelineEditsAvailable && videoPlayer.duration > 0
                minimumButtonWidth: 0
                iconSource: root.iconUrl("scissors")
                text: qsTr("Split")
                tooltipText: backend.timelineEditsAvailable
                    ? qsTr("Split the recording at the playhead (S)")
                    : qsTr("Cuts and speed changes need GIF or WebP on this system")
                onClicked: {
                    videoPlayer.pause()
                    if (!backend.splitAt(videoPlayer.position))
                        root.showButtonTooltip(qsTr("Too close to another split or an end"), this, true)
                }
            }
            SegmentButton {
                objectName: "previewPartCut"
                enabled: backend.timelineEditsAvailable && root.cutTargetPartData !== null
                         && backend.timelineParts.length > 1
                minimumButtonWidth: 0
                iconSource: root.iconUrl("trash-2")
                text: root.cutTargetPartData && root.cutTargetPartData.deleted ? qsTr("Keep Part") : qsTr("Cut Part")
                tooltipText: root.cutTargetsSelection
                    ? (root.cutTargetPartData.deleted ? qsTr("Put the selected clip back (Delete)")
                                                      : qsTr("Leave the selected clip out of the export (Delete)"))
                    : root.cutTargetPartData && root.cutTargetPartData.deleted
                      ? qsTr("Put the part at the playhead back (Delete)")
                      : qsTr("Leave the part at the playhead out of the export (Delete)")
                selected: root.cutTargetPartData !== null && root.cutTargetPartData.deleted
                onClicked: root.toggleCutTarget()
            }
            SegmentButton {
                objectName: "previewAddEffect"
                minimumButtonWidth: 0
                iconSource: root.iconUrl("plus")
                trailingIconSource: root.iconUrl("chevron-down")
                text: qsTr("Add effect")
                tooltipText: qsTr("Zoom, mask, text or image at the playhead")
                onClicked: if (root.prepareNewEffect()) effectMenu.open()
            }
            // Exact trim entry ("m:ss.mmm") while a trim range exists.
            Row {
                id: trimFields
                objectName: "previewTrimFields"
                visible: backend.hasTrim
                height: ComponentTokens.recordingPreviewControlButtonHeight
                spacing: SemanticTokens.spacing4
                leftPadding: SemanticTokens.spacing8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Trim")
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    rightPadding: SemanticTokens.spacing4
                }
                TimeField {
                    id: trimStartField
                    objectName: "previewTrimStartField"
                    anchors.verticalCenter: parent.verticalCenter
                    accessibleName: qsTr("Trim start")
                    timeMs: backend.trimStart
                    onCommitted: function(ms) { backend.setTrimFromInput(ms, backend.trimEnd) }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "–"
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeSmall
                }
                TimeField {
                    id: trimEndField
                    objectName: "previewTrimEndField"
                    anchors.verticalCenter: parent.verticalCenter
                    accessibleName: qsTr("Trim end")
                    timeMs: backend.trimEnd
                    onCommitted: function(ms) { backend.setTrimFromInput(backend.trimStart, ms) }
                }
            }
            Popup {
                id: effectMenu
                focus: true
                y: 32
                padding: 8
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Rectangle { color: root.bgPanel; border.color: root.borderColor; radius: 8 }
                contentItem: Column {
                    spacing: 4
                SegmentButton {
                    objectName: "previewAutoZoom"
                    visible: backend.zoomAvailable
                    enabled: backend.autoZoomAvailable
                    minimumButtonWidth: 0
                    text: qsTr("Auto Zoom")
                    tooltipText: backend.autoZoomAvailable
                        ? qsTr("Zoom in on clicks automatically. Zooms you added or changed are kept.")
                        : qsTr("Auto zoom needs recorded clicks; add zooms by hand instead")
                    onClicked: {
                        if (!root.prepareNewEffect()) return
                        const count = backend.generateAutoZoom()
                        effectMenu.close()
                        // Say what happened right at the button.
                        root.showButtonTooltip(count > 0 ? qsTr("Zooms added: %1").arg(count)
                                                         : qsTr("No clicks to zoom in on"), this, true)
                    }
                }
                SegmentButton {
                    objectName: "previewAddZoom"
                    visible: backend.zoomAvailable
                    minimumButtonWidth: 0
                    text: qsTr("Add Zoom")
                    tooltipText: qsTr("Add a zoom at the playhead")
                    onClicked: {
                        if (!root.prepareNewEffect()) return
                        effectMenu.close()
                        root.inspectorOpen = true
                        videoPlayer.pause()
                        // Existing zooms are never changed to make room.
                        if (backend.addZoomAt(videoPlayer.position) < 0)
                            root.showButtonTooltip(qsTr("No room for a new zoom here"), this, true)
                    }
                }
                SegmentButton {
                    objectName: "previewAddMask"
                    enabled: backend.masksAvailable
                    minimumButtonWidth: 0
                    text: qsTr("Add Mask")
                    tooltipText: backend.masksAvailable
                        ? qsTr("Cover a sensitive area in the exported file")
                        : qsTr("Masks need GIF or WebP on this system")
                    onClicked: {
                        if (!root.prepareNewEffect()) return
                        effectMenu.close()
                        root.inspectorOpen = true
                        videoPlayer.pause()
                        backend.selectedZoom = -1
                        backend.selectedPointerHide = -1
                        backend.addMask()
                    }
                }
                SegmentButton {
                    objectName: "previewAddText"
                    enabled: backend.overlaysAvailable
                    minimumButtonWidth: 0
                    text: qsTr("Add Text")
                    tooltipText: backend.overlaysAvailable
                        ? qsTr("A title or caption at the playhead")
                        : qsTr("Overlays need GIF or WebP on this system")
                    onClicked: {
                        if (!root.prepareNewEffect()) return
                        effectMenu.close()
                        root.inspectorOpen = true
                        videoPlayer.pause()
                        backend.selectedZoom = -1
                        backend.selectedMask = -1
                        backend.selectedPointerHide = -1
                        // Shown past its fade-in, so the new title is visible
                        // while it is edited (PR #201 review).
                        root.showOverlay(backend.addTextOverlay(videoPlayer.position))
                    }
                }
                SegmentButton {
                    objectName: "previewAddImage"
                    enabled: backend.overlaysAvailable
                    minimumButtonWidth: 0
                    text: qsTr("Add Image")
                    tooltipText: backend.overlaysAvailable
                        ? qsTr("An image such as a logo, for the whole recording")
                        : qsTr("Overlays need GIF or WebP on this system")
                    onClicked: {
                        if (!root.prepareNewEffect()) return
                        effectMenu.close()
                        root.inspectorOpen = true
                        videoPlayer.pause()
                        backend.selectedZoom = -1
                        backend.selectedMask = -1
                        backend.selectedPointerHide = -1
                        backend.chooseOverlayImage()
                    }
                }
                }
            }
        }
        Item {
            id: timelineArea
            objectName: "previewTimeline"
            clip: true
            Layout.fillWidth: true
            // A fixed height: adding or removing an effect lane never resizes the
            // preview. Lanes past the visible ones scroll.
            readonly property int visibleLanes: root.height < 600 ? 2 : 3
            Layout.preferredHeight: effectTracks.y + visibleLanes * laneHeight + 6
            Layout.leftMargin: SemanticTokens.spacing16
            Layout.rightMargin: SemanticTokens.spacing16

            readonly property real labelWidth: root.wideEditor ? 112 : 92
            readonly property real rulerHeight: 22
            readonly property real trackY: rulerHeight + 4
            readonly property real trackHeight: 30
            readonly property real laneHeight: 30
            readonly property real handleWidth: 8
            readonly property real dur: videoPlayer.duration > 0 ? videoPlayer.duration : 1

            function posToX(ms) {
                return (ms / dur) * trackRect.width + trackRect.x
            }

            function xToPos(x) {
                var fraction = Math.max(0, Math.min(1, (x - trackRect.x) / trackRect.width))
                return Math.round(fraction * dur)
            }

            // Effects of one kind share a lane; one that would overlap another *on
            // screen* gets the next row, so every segment stays visible and can be
            // grabbed on its own. A segment is drawn at least segmentMinWidth wide,
            // so a short one reaches further than its time range.
            readonly property real segmentMinWidth: 8
            readonly property real segmentGap: 2
            function packRows(items) {
                const order = items.map((item, i) => i).sort((a, b) => items[a].start - items[b].start)
                const msPerPixel = dur / Math.max(1, trackRect.width)
                const ends = []
                const row = new Array(items.length)
                for (const i of order) {
                    const start = items[i].start
                    const end = Math.max(items[i].end < 0 ? dur : items[i].end, start + segmentMinWidth * msPerPixel)
                                + segmentGap * msPerPixel
                    let r = ends.findIndex(e => e <= start)
                    if (r < 0) { r = ends.length; ends.push(end) } else ends[r] = end
                    row[i] = r
                }
                return { row: row, count: ends.length }
            }
            readonly property var lanes: {
                const zooms = packRows(backend.zoomSegments)
                const zoomRows = zooms.count
                const masks = packRows(backend.masks)
                const overlays = packRows(backend.overlays)
                const pointers = packRows(backend.pointerHiddenRanges)
                return {
                    zoomBase: 0, maskBase: zoomRows, overlayBase: zoomRows + masks.count,
                    pointerBase: zoomRows + masks.count + overlays.count,
                    zoom: zooms, mask: masks, overlay: overlays, pointer: pointers,
                    groups: [
                        { name: qsTr("Zoom"), icon: "zoom-in", base: 0, count: zoomRows },
                        { name: qsTr("Mask"), icon: "square-dashed", base: zoomRows, count: masks.count },
                        { name: qsTr("Text & image"), icon: "text", base: zoomRows + masks.count, count: overlays.count },
                        { name: qsTr("Pointer"), icon: "mouse-pointer", base: zoomRows + masks.count + overlays.count,
                          count: pointers.count }
                    ].filter(group => group.count > 0),
                    total: zoomRows + masks.count + overlays.count + pointers.count
                }
            }
            // A selected effect is scrolled into view, wherever it was selected from.
            function revealLane(row) {
                const top = row * laneHeight
                const bottom = top + laneHeight
                if (top < effectTracks.contentY)
                    effectTracks.contentY = top
                else if (bottom > effectTracks.contentY + effectTracks.height)
                    effectTracks.contentY = Math.max(0, Math.min(bottom - effectTracks.height,
                                                                 effectTracks.contentHeight - effectTracks.height))
            }
            function revealSelection() {
                if (backend.selectedZoom >= 0) revealLane(lanes.zoomBase + (lanes.zoom.row[backend.selectedZoom] || 0))
                else if (backend.selectedMask >= 0) revealLane(lanes.maskBase + (lanes.mask.row[backend.selectedMask] || 0))
                else if (backend.selectedOverlay >= 0)
                    revealLane(lanes.overlayBase + (lanes.overlay.row[backend.selectedOverlay] || 0))
                else if (backend.selectedPointerHide >= 0)
                    revealLane(lanes.pointerBase + (lanes.pointer.row[backend.selectedPointerHide] || 0))
            }
            Connections {
                target: backend
                function onSelectedZoomChanged() { Qt.callLater(timelineArea.revealSelection) }
                function onSelectedMaskChanged() { Qt.callLater(timelineArea.revealSelection) }
                function onSelectedOverlayChanged() { Qt.callLater(timelineArea.revealSelection) }
                function onSelectedPointerHideChanged() { Qt.callLater(timelineArea.revealSelection) }
            }
            // An edit can repack the lanes; keep the selected effect in view.
            onLanesChanged: Qt.callLater(timelineArea.revealSelection)

            // Ruler step: the shortest one that keeps labels apart.
            readonly property real tickMs: {
                const steps = [100, 250, 500, 1000, 2000, 5000, 10000, 15000, 30000, 60000, 120000, 300000, 600000]
                const minimumGap = 72
                for (const step of steps) {
                    if (step / dur * trackRect.width >= minimumGap) return step
                }
                return steps[steps.length - 1]
            }

            // Lane names, aligned with the rows they describe.
            Row {
                y: timelineArea.trackY
                height: timelineArea.trackHeight
                spacing: 6
                SvgIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    source: root.iconUrl("film")
                    iconSize: 14
                    color: root.textSecondary
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Video")
                    color: root.textPrimary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.weight: SemanticTokens.fontWeightMedium
                }
            }

            // Ruler.
            Repeater {
                model: Math.floor(timelineArea.dur / timelineArea.tickMs) + 1
                Item {
                    required property int index
                    readonly property real ms: index * timelineArea.tickMs
                    x: timelineArea.posToX(ms)
                    width: 1
                    height: timelineArea.rulerHeight
                    Rectangle { anchors.bottom: parent.bottom; width: 1; height: 5; color: root.borderColor }
                    Text {
                        // A label that would run past the track is left out, never overlapped.
                        visible: parent.x + implicitWidth + 3 <= trackRect.x + trackRect.width + 2
                        x: 3
                        y: 2
                        text: timelineArea.tickMs < 1000 ? backend.formatPreciseTime(parent.ms) : backend.formatTime(parent.ms)
                        color: root.textSecondary
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.features: { "tnum": 1 }
                    }
                }
            }
            Rectangle {
                y: timelineArea.rulerHeight
                width: parent.width
                height: 1
                color: root.borderColor
            }

            // Video lane: the recording's parts as clips.
            Rectangle {
                id: trackRect
                x: timelineArea.labelWidth
                y: timelineArea.trackY + 3
                width: timelineArea.width - timelineArea.labelWidth - Math.max(timelineArea.handleWidth, effectScrollBar.width + 4)
                height: timelineArea.trackHeight - 6
                radius: 5
                color: "transparent"
            }
            // Before any split the recording is a single clip.
            Rectangle {
                visible: backend.timelineParts.length === 0
                x: trackRect.x
                y: trackRect.y
                width: trackRect.width
                height: trackRect.height
                radius: 5
                color: ComponentTokens.editorClip
                border.width: 1
                border.color: ComponentTokens.editorClipBorder
            }

            // Cut-out parts are shaded, parts at another speed are labelled (#185).
            Repeater {
                model: backend.timelineParts
                Rectangle {
                    required property int index
                    required property var modelData
                    readonly property bool chosen: root.selectedPart === index
                    objectName: "previewTimelinePart" + index
                    x: timelineArea.posToX(modelData.start) + 1
                    y: trackRect.y
                    width: Math.max(1, timelineArea.posToX(modelData.end) - timelineArea.posToX(modelData.start) - 2)
                    height: trackRect.height
                    z: 1
                    radius: 5
                    color: chosen ? ComponentTokens.editorSelection
                         : modelData.deleted ? "transparent" : ComponentTokens.editorClip
                    border.width: chosen ? 2 : 1
                    border.color: chosen ? root.accent : ComponentTokens.editorClipBorder
                    opacity: modelData.deleted && !chosen ? 0.55 : 1
                    // Cut clips are outlined and crossed, not only dimmed.
                    Rectangle {
                        visible: parent.modelData.deleted
                        anchors.centerIn: parent
                        width: Math.hypot(parent.width, parent.height) - 6
                        height: 1
                        rotation: Math.atan2(parent.height, parent.width) * 180 / Math.PI
                        color: root.textSecondary
                    }
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        visible: parent.width > 28
                        text: parent.modelData.deleted ? qsTr("Cut")
                            : parent.modelData.speed !== 1 ? root.speedLabel(parent.modelData.speed) : ""
                        color: parent.modelData.deleted ? root.textSecondary : root.textPrimary
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.weight: SemanticTokens.fontWeightMedium
                        elide: Text.ElideRight
                    }
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.selected: chosen
                    Accessible.onPressAction: root.selectPart(index)
                    Keys.onReturnPressed: root.selectPart(index)
                    Keys.onSpacePressed: root.selectPart(index)
                    FocusFrame { anchors.fill: parent; showFocus: parent.activeFocus }
                    Accessible.name: modelData.deleted
                        ? qsTr("Cut from %1 to %2").arg(backend.formatPreciseTime(modelData.start))
                                                   .arg(backend.formatPreciseTime(modelData.end))
                        : qsTr("Part from %1 to %2 at %3").arg(backend.formatPreciseTime(modelData.start))
                                                          .arg(backend.formatPreciseTime(modelData.end))
                                                          .arg(root.speedLabel(modelData.speed))
                }
            }

            Rectangle {
                visible: backend.hasTrim
                x: trackRect.x
                y: trackRect.y
                width: Math.max(0, timelineArea.posToX(backend.trimStart) - trackRect.x)
                height: trackRect.height
                z: 2
                color: root.trimOverlayColor
                radius: 5
            }

            Rectangle {
                visible: backend.hasTrim
                x: timelineArea.posToX(backend.trimEnd)
                y: trackRect.y
                width: Math.max(0, trackRect.x + trackRect.width - timelineArea.posToX(backend.trimEnd))
                height: trackRect.height
                z: 2
                color: root.trimOverlayColor
                radius: 5
            }

            Rectangle {
                id: trimStartHandle
                visible: backend.hasTrim
                x: timelineArea.posToX(backend.trimStart) - width / 2
                y: trackRect.y - 3
                z: 3
                width: timelineArea.handleWidth
                height: trackRect.height + 6
                radius: 2
                color: root.isDraggingTrimStart ? root.accentHover : root.accent
            }

            Rectangle {
                id: trimEndHandle
                visible: backend.hasTrim
                x: timelineArea.posToX(backend.trimEnd) - width / 2
                y: trackRect.y - 3
                z: 3
                width: timelineArea.handleWidth
                height: trackRect.height + 6
                radius: 2
                color: root.isDraggingTrimEnd ? root.accentHover : root.accent
            }

            // Effect lanes scroll under the fixed ruler and video lane.
            Rectangle {
                y: effectTracks.y - 1
                width: parent.width
                height: 1
                color: root.borderColor
            }
            Flickable {
                id: effectTracks
                objectName: "previewEffectTracks"
                y: timelineArea.trackY + timelineArea.trackHeight + 4
                width: parent.width
                height: Math.max(0, parent.height - y)
                clip: true
                contentWidth: width
                contentHeight: Math.max(1, timelineArea.lanes.total) * timelineArea.laneHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { id: effectScrollBar }
                // A click on an empty lane returns to the whole-recording settings.
                MouseArea {
                    width: parent.width
                    height: Math.max(effectTracks.height, effectTracks.contentHeight)
                    onClicked: root.openSection("")
                }
                // Row separators.
                Repeater {
                    model: Math.max(0, timelineArea.lanes.total - 1)
                    Rectangle {
                        required property int index
                        y: (index + 1) * timelineArea.laneHeight
                        width: effectTracks.width
                        height: 1
                        color: root.borderColor
                        opacity: 0.6
                    }
                }
                Repeater {
                    model: timelineArea.lanes.groups
                    Row {
                        required property var modelData
                        y: modelData.base * timelineArea.laneHeight
                        height: timelineArea.laneHeight
                        spacing: 6
                        SvgIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            source: root.iconUrl(parent.modelData.icon)
                            iconSize: 14
                            color: root.textSecondary
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.modelData.name
                            color: root.textPrimary
                            font.pixelSize: SemanticTokens.fontSizeCaption
                            font.weight: SemanticTokens.fontWeightMedium
                        }
                    }
                }
                Text {
                    visible: timelineArea.lanes.total === 0
                    x: trackRect.x
                    height: timelineArea.laneHeight
                    width: trackRect.width
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: qsTr("No effects yet. Use Add effect to zoom, mask, or add text at the playhead.")
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                }
            }

            // Split points; drag to move one, double-click to join its parts.
            Repeater {
                id: splitMarkers
                model: backend.timelineSplits
                // A moved, removed or rebuilt marker takes the keyboard with it:
                // give it to the marker now at that place, the last one, or the
                // editor, so its shortcuts keep working.
                function refocus(index) {
                    const marker = itemAt(Math.min(index, count - 1))
                    if (marker) marker.forceActiveFocus(Qt.OtherFocusReason)
                    else root.forceActiveFocus(Qt.OtherFocusReason)
                }
                Rectangle {
                    required property int index
                    required property var modelData
                    objectName: "previewTimelineSplit" + index
                    // While dragged it moves here only: changing the model on
                    // every move would rebuild this delegate and end the drag
                    // (PR #199 review). The edit is made once, on release.
                    property real dragMs: -1
                    x: timelineArea.posToX(dragMs >= 0 ? dragMs : modelData) - width / 2
                    y: trackRect.y - 3
                    width: 3
                    height: trackRect.height + 6
                    radius: 1
                    z: 4
                    color: root.textPrimary
                    // Reachable by keyboard: arrows move it (Shift: 1 s), Delete removes it.
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Split at %1").arg(backend.formatPreciseTime(modelData))
                    Accessible.description: qsTr("Arrow keys move the split; Delete joins the two clips")
                    Accessible.onPressAction: forceActiveFocus()
                    Component.onDestruction: if (activeFocus) Qt.callLater(splitMarkers.refocus, index)
                    // One undo step per press, a held key included: its first move
                    // records the step and each repeat is folded into it at once,
                    // so nothing waits for a release that focus may take elsewhere.
                    // moveSplit() rebuilds this marker at once: everything it
                    // needs afterwards is read before.
                    function nudge(event, direction) {
                        event.accepted = true
                        const editor = backend
                        const preview = root
                        // A new press never folds into an older step.
                        if (!event.isAutoRepeat) preview.splitNudgeHeld = false
                        const step = (event.modifiers & Qt.ShiftModifier) ? 1000 : 100
                        const legal = editor.legalSplitPosition(index, modelData + direction * step)
                        if (legal < 0 || legal === modelData || !editor.moveSplit(index, legal)) return
                        if (preview.splitNudgeHeld) {
                            editor.amendTimelineEdit()
                        } else {
                            editor.commitTimelineEdit()
                            preview.splitNudgeHeld = true
                        }
                    }
                    Keys.onLeftPressed: function(event) { nudge(event, -1) }
                    Keys.onRightPressed: function(event) { nudge(event, 1) }
                    Keys.onReleased: function(event) {
                        if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right) && !event.isAutoRepeat)
                            root.splitNudgeHeld = false
                    }
                    Keys.onDeletePressed: function(event) { event.accepted = true; backend.removeSplit(index) }
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Backspace) {
                            event.accepted = true
                            backend.removeSplit(index)
                        }
                    }
                    FocusFrame { anchors.fill: parent; anchors.margins: -3; showFocus: parent.activeFocus }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: CursorTokens.resizeHorizontal
                        onPressed: {
                            parent.dragMs = -1
                            root.splitDragItem = parent
                            videoPlayer.pause()
                        }
                        onPositionChanged: function(mouse) {
                            if (root.splitDragItem !== parent) return // cancelled with Escape
                            const ms = timelineArea.xToPos(mapToItem(timelineArea, mouse.x, mouse.y).x)
                            const legal = backend.legalSplitPosition(parent.index, ms)
                            if (legal >= 0) parent.dragMs = legal
                        }
                        function finishDrag() {
                            const target = parent.dragMs
                            const active = root.splitDragItem === parent
                            parent.dragMs = -1
                            if (active) root.splitDragItem = null
                            if (active && target >= 0 && backend.moveSplit(parent.index, target))
                                backend.commitTimelineEdit()
                        }
                        onReleased: finishDrag()
                        onCanceled: {
                            parent.dragMs = -1
                            if (root.splitDragItem === parent) root.splitDragItem = null
                        }
                        onDoubleClicked: backend.removeSplit(parent.index)
                    }
                }
            }

            // Zooms never overlap in time, but very short ones can on screen.
            Repeater {
                model: backend.zoomSegments
                parent: effectTracks.contentItem
                EditorTimelineSegment {
                    required property int index
                    required property var modelData
                    objectName: "previewZoomSegment" + index
                    y: (timelineArea.lanes.zoomBase + (timelineArea.lanes.zoom.row[index] || 0)) * timelineArea.laneHeight + 3
                    z: 2
                    startMs: modelData.start
                    endMs: modelData.end < 0 ? timelineArea.dur : modelData.end
                    exported: root.rangeExported(modelData.start, modelData.end)
                    lowerMs: index > 0 ? backend.zoomSegments[index - 1].end : 0
                    upperMs: index + 1 < backend.zoomSegments.length ? backend.zoomSegments[index + 1].start : timelineArea.dur
                    minimumDuration: 500
                    pixelsPerMs: trackRect.width / timelineArea.dur
                    minimumDisplayWidth: timelineArea.segmentMinWidth
                    trackX: trackRect.x
                    selected: backend.selectedZoom === index
                    opacity: backend.zoomEnabled ? 1 : 0.5
                    fillColor: ComponentTokens.editorZoomFill
                    edgeColor: ComponentTokens.editorZoomEdge
                    iconSource: root.iconUrl("zoom-in")
                    label: qsTr("Zoom %1").arg(root.speedLabel(modelData.scale))
                    accessibleName: qsTr("Zoom %1× from %2 to %3").arg(Number(modelData.scale).toLocaleString(Qt.locale(), "g", 3))
                        .arg(backend.formatPreciseTime(startMs)).arg(backend.formatPreciseTime(endMs))
                    prepareSelection: function() { return root.prepareEffectInteraction("zoom", index) }
                    onActivated: {
                        if (root.selectEffect("zoom", index)) {
                            videoPlayer.pause()
                            videoPlayer.seek(Math.min(endMs - 1, startMs + 100))
                        }
                    }
                    onRangeEdited: function(start, end) { root.editEffectRange("zoom", index, start, end) }
                }
            }

            Repeater {
                model: backend.masks
                parent: effectTracks.contentItem
                EditorTimelineSegment {
                    required property int index
                    required property var modelData
                    objectName: "previewMaskSegment" + index
                    y: (timelineArea.lanes.maskBase + (timelineArea.lanes.mask.row[index] || 0)) * timelineArea.laneHeight + 3
                    z: 2
                    openEnded: modelData.end < 0
                    startMs: modelData.start
                    endMs: modelData.end < 0 ? timelineArea.dur : modelData.end
                    exported: root.rangeExported(modelData.start, modelData.end)
                    lowerMs: 0
                    upperMs: timelineArea.dur
                    minimumDuration: 1
                    pixelsPerMs: trackRect.width / timelineArea.dur
                    minimumDisplayWidth: timelineArea.segmentMinWidth
                    trackX: trackRect.x
                    selected: backend.selectedMask === index
                    fillColor: ComponentTokens.editorMaskFill
                    edgeColor: ComponentTokens.editorMaskEdge
                    iconSource: root.iconUrl("square-dashed")
                    label: qsTr("Mask · %1").arg(root.maskStyleLabels[modelData.style] || "")
                    accessibleName: qsTr("Mask from %1 to %2").arg(backend.formatPreciseTime(modelData.start))
                        .arg(modelData.end < 0 ? qsTr("the end") : backend.formatPreciseTime(modelData.end))
                    prepareSelection: function() { return root.prepareEffectInteraction("mask", index) }
                    onActivated: {
                        if (root.selectEffect("mask", index)) {
                            videoPlayer.pause()
                            videoPlayer.seek(Math.min(endMs - 1, startMs + 100))
                        }
                    }
                    onRangeEdited: function(start, end) { root.editEffectRange("mask", index, start, end) }
                }
            }

            // Text and image overlays (#188).
            Repeater {
                model: backend.overlays
                parent: effectTracks.contentItem
                EditorTimelineSegment {
                    required property int index
                    required property var modelData
                    objectName: "previewOverlaySegment" + index
                    y: (timelineArea.lanes.overlayBase + (timelineArea.lanes.overlay.row[index] || 0)) * timelineArea.laneHeight + 3
                    z: 2
                    openEnded: modelData.end < 0
                    startMs: modelData.start
                    endMs: modelData.end < 0 ? timelineArea.dur : modelData.end
                    exported: root.rangeExported(modelData.start, modelData.end)
                    lowerMs: 0
                    upperMs: timelineArea.dur
                    minimumDuration: 100
                    pixelsPerMs: trackRect.width / timelineArea.dur
                    minimumDisplayWidth: timelineArea.segmentMinWidth
                    trackX: trackRect.x
                    selected: backend.selectedOverlay === index
                    opacity: backend.overlaysAvailable ? 1 : 0.5
                    fillColor: ComponentTokens.editorOverlayFill
                    edgeColor: ComponentTokens.editorOverlayEdge
                    iconSource: root.iconUrl(modelData.kind === 0 ? "text" : "image")
                    label: modelData.kind === 0 ? (modelData.text.length > 0 ? modelData.text : qsTr("Text")) : qsTr("Image")
                    accessibleName: modelData.kind === 0
                        ? qsTr("Text overlay \"%1\" from %2 to %3").arg(modelData.text)
                              .arg(backend.formatPreciseTime(modelData.start))
                              .arg(modelData.end < 0 ? qsTr("the end") : backend.formatPreciseTime(modelData.end))
                        : qsTr("Image overlay from %1 to %2").arg(backend.formatPreciseTime(modelData.start))
                              .arg(modelData.end < 0 ? qsTr("the end") : backend.formatPreciseTime(modelData.end))
                    prepareSelection: function() { return root.prepareEffectInteraction("overlay", index) }
                    onActivated: {
                        if (root.selectEffect("overlay", index)) {
                            videoPlayer.pause()
                            videoPlayer.seek(Math.min(endMs - 1, startMs + 100))
                        }
                    }
                    onRangeEdited: function(start, end) { root.editEffectRange("overlay", index, start, end) }
                }
            }

            // Ranges without a pointer (#186).
            Repeater {
                model: backend.pointerHiddenRanges
                parent: effectTracks.contentItem
                EditorTimelineSegment {
                    required property int index
                    required property var modelData
                    objectName: "previewPointerHideSegment" + index
                    y: (timelineArea.lanes.pointerBase + (timelineArea.lanes.pointer.row[index] || 0)) * timelineArea.laneHeight + 3
                    z: 2
                    startMs: modelData.start
                    endMs: modelData.end < 0 ? timelineArea.dur : modelData.end
                    exported: root.rangeExported(modelData.start, modelData.end)
                    lowerMs: 0
                    upperMs: timelineArea.dur
                    minimumDuration: 100
                    pixelsPerMs: trackRect.width / timelineArea.dur
                    minimumDisplayWidth: timelineArea.segmentMinWidth
                    trackX: trackRect.x
                    selected: backend.selectedPointerHide === index
                    opacity: backend.cursorEffectsAvailable ? 1 : 0.5
                    fillColor: ComponentTokens.editorPointerFill
                    edgeColor: ComponentTokens.editorPointerEdge
                    iconSource: root.iconUrl("mouse-pointer")
                    label: qsTr("Pointer hidden")
                    accessibleName: qsTr("Pointer hidden from %1 to %2").arg(backend.formatPreciseTime(modelData.start))
                        .arg(modelData.end < 0 ? qsTr("the end") : backend.formatPreciseTime(modelData.end))
                    prepareSelection: function() { return root.prepareEffectInteraction("pointer", index) }
                    onActivated: {
                        if (root.selectEffect("pointer", index)) {
                            videoPlayer.pause()
                            videoPlayer.seek(Math.min(endMs - 1, startMs + 100))
                        }
                    }
                    onRangeEdited: function(start, end) { root.editEffectRange("pointer", index, start, end) }
                }
            }

            MouseArea {
                id: scrubber
                objectName: "previewScrubber"
                width: parent.width
                height: timelineArea.trackY + timelineArea.trackHeight
                hoverEnabled: true
                preventStealing: true
                // Where the video lane is, for keyboard-free tests and tools.
                readonly property real trackLeft: trackRect.x
                readonly property real trackWidth: trackRect.width
                readonly property real laneCenterY: trackRect.y + trackRect.height / 2
                // Seeking preserves the active edit. Selecting a clip is an explicit gesture.
                onDoubleClicked: function(mouse) {
                    if (!draggingTrimStart && !draggingTrimEnd)
                        root.selectPart(backend.timelinePartAt(timelineArea.xToPos(mouse.x)))
                }

                property bool draggingTrimStart: false
                property bool draggingTrimEnd: false
                property bool wasPlayingBeforeScrub: false

                function resetInteractionState() {
                    var shouldResume = root.isScrubbing && wasPlayingBeforeScrub
                    draggingTrimStart = false
                    draggingTrimEnd = false
                    root.isDraggingTrimStart = false
                    root.isDraggingTrimEnd = false
                    root.isScrubbing = false
                    wasPlayingBeforeScrub = false
                    if (shouldResume) {
                        videoPlayer.play()
                    }
                }

                onPressed: function(mouse) {
                    var mx = mouse.x
                    wasPlayingBeforeScrub = false

                    if (backend.hasTrim) {
                        var tsX = timelineArea.posToX(backend.trimStart)
                        var teX = timelineArea.posToX(backend.trimEnd)

                        if (Math.abs(mx - tsX) < 12) {
                            draggingTrimStart = true
                            root.isDraggingTrimStart = true
                            return
                        }
                        if (Math.abs(mx - teX) < 12) {
                            draggingTrimEnd = true
                            root.isDraggingTrimEnd = true
                            return
                        }
                    }

                    root.isScrubbing = true
                    wasPlayingBeforeScrub = videoPlayer.playing
                    videoPlayer.pause()
                    videoPlayer.seek(timelineArea.xToPos(mx))
                }

                onPositionChanged: function(mouse) {
                    var mx = mouse.x
                    if (draggingTrimStart) {
                        var pos = timelineArea.xToPos(mx)
                        backend.trimStart = Math.max(0, Math.min(pos, backend.trimEnd - 100))
                    } else if (draggingTrimEnd) {
                        var pos2 = timelineArea.xToPos(mx)
                        backend.trimEnd = Math.max(backend.trimStart + 100, Math.min(pos2, videoPlayer.duration))
                    } else if (root.isScrubbing) {
                        videoPlayer.seek(timelineArea.xToPos(mx))
                    }
                }

                onReleased: {
                    const trimmed = draggingTrimStart || draggingTrimEnd
                    resetInteractionState()
                    if (trimmed)
                        backend.commitTrimEdit()
                }

                // A lost grab or Escape leaves the trim as it was before the drag.
                function cancelTrimDrag() {
                    const trimmed = draggingTrimStart || draggingTrimEnd
                    resetInteractionState()
                    if (trimmed)
                        backend.cancelTrimEdit()
                }
                onCanceled: cancelTrimDrag()
            }

            // The playhead crosses every lane.
            Item {
                id: playhead
                objectName: "previewPlayhead"
                x: timelineArea.posToX(videoPlayer.position)
                width: 1
                height: parent.height
                z: 20
                Rectangle {
                    x: -1
                    y: timelineArea.rulerHeight - 4
                    width: 2
                    height: parent.height - y
                    color: root.accent
                }
                Rectangle {
                    x: -5
                    y: timelineArea.rulerHeight - 12
                    width: 10
                    height: 10
                    radius: 3
                    color: root.accent
                }
            }
        }

        Item {
            id: controlRowArea
            parent: editorShell.inspectorContent
            width: parent.width
            height: Math.max(formatRow.visible ? formatRow.height : 0,
                zoomPanel.visible ? zoomPanel.height : 0, maskPanel.visible ? maskPanel.height : 0,
                pointerPanel.visible ? pointerPanel.height : 0, appearancePanel.visible ? appearancePanel.height : 0,
                keysPanel.visible ? keysPanel.height : 0, audioPanel.visible ? audioPanel.height : 0,
                overlayPanel.visible ? overlayPanel.height : 0, pointerHidePanel.visible ? pointerHidePanel.height : 0,
                cropPanel.visible ? cropPanel.height : 0, clipPanel.visible ? clipPanel.height : 0) + 24
            Column {
                id: formatRow
                objectName: "previewFormatRow"
                x: 8
                y: SemanticTokens.spacing12
                width: parent.width - 16
                spacing: 2
                readonly property bool shown: root.selectedPart < 0 && !cropOverlay.editing && !(backend.selectedZoom >= 0)
                                              && !(backend.selectedMask >= 0) && !pointerPanel.visible
                                              && !pointerHidePanel.visible
                                              && !appearancePanel.visible && !keysPanel.visible && !audioPanel.visible
                                              && !overlayPanel.visible
                visible: shown
                enabled: shown
                Accessible.ignored: !shown

                Text {
                    width: parent.width
                    text: qsTr("These settings apply to the whole recording. Select a clip or effect on the timeline to edit just that part.")
                    leftPadding: 8
                    rightPadding: 8
                    wrapMode: Text.WordWrap
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    bottomPadding: SemanticTokens.spacing4
                }

                CategoryRow {
                    objectName: "previewAppearanceOptions"
                    available: backend.appearanceAvailable
                    iconSource: root.iconUrl("frame")
                    text: qsTr("Canvas")
                    summary: backend.appearanceEnabled
                        ? qsTr("Frame on · padding %1").arg(root.appearanceAmountLabels[backend.appearancePadding])
                        : qsTr("No frame")
                    unavailableReason: qsTr("A frame needs GIF or WebP on this system.")
                    tooltipText: qsTr("A background, padding, rounded corners, a shadow and a border around the video")
                    selected: backend.appearanceActive
                    onClicked: root.openSection("appearance")
                }
                CategoryRow {
                    objectName: "previewPointerOptions"
                    available: backend.hasPointerData && backend.cursorEffectsAvailable
                    iconSource: root.iconUrl("mouse-pointer")
                    text: qsTr("Pointer")
                    summary: !backend.showCursor ? qsTr("Hidden")
                        : backend.pointerHiddenRanges.length > 0
                          ? qsTr("Shown · %1 · %n hidden range(s)", "", backend.pointerHiddenRanges.length)
                                .arg(root.speedLabel(backend.cursorScale))
                          : qsTr("Shown · %1").arg(root.speedLabel(backend.cursorScale))
                    unavailableReason: !backend.hasPointerData ? qsTr("No pointer data was recorded.")
                        : qsTr("Pointer effects need GIF or WebP on this system.")
                    tooltipText: qsTr("Size, smoothing, click highlights, hide when idle, motion blur and a still ending")
                    selected: backend.cursorIdleHide > 0 || backend.cursorMotionBlur > 0
                              || backend.cursorStillEnd > 0 || backend.pointerHiddenRanges.length > 0
                    onClicked: root.openSection("pointer")
                }
                CategoryRow {
                    objectName: "previewAudioOptions"
                    available: backend.hasSeparateTracks && backend.audioMixAvailable
                    iconSource: root.iconUrl("volume-2")
                    text: qsTr("Audio")
                    summary: qsTr("Microphone %1 · System %2")
                        .arg(backend.microphoneMuted ? qsTr("muted") : root.gainLabel(backend.microphoneGainDb))
                        .arg(backend.systemAudioMuted ? qsTr("muted") : root.gainLabel(backend.systemAudioGainDb))
                    unavailableReason: !backend.hasSeparateTracks
                        ? qsTr("No separate microphone and system tracks. The recorded audio is kept as it is.")
                        : backend.selectedFormat !== 0
                          ? qsTr("GIF and WebP have no sound. Choose MP4 to adjust the audio.")
                          : qsTr("Mixing the two tracks is not supported for MP4 on this system yet.")
                    tooltipText: qsTr("Microphone and system audio volume, mute, and noise reduction")
                    selected: backend.audioMixAvailable && (backend.microphoneGainDb !== 0 || backend.systemAudioGainDb !== 0
                              || backend.microphoneMuted || backend.systemAudioMuted || backend.microphoneNoiseReduction)
                    onClicked: root.openSection("audio")
                }
                CategoryRow {
                    objectName: "previewKeyOptions"
                    available: backend.hasKeyData && backend.keyOverlayAvailable
                    iconSource: root.iconUrl("keyboard")
                    text: qsTr("Keys")
                    summary: backend.keyOverlayEnabled
                        ? qsTr("Shown · %n shortcut(s) recorded", "", backend.recordedKeyCount)
                        : qsTr("Off · %n shortcut(s) recorded", "", backend.recordedKeyCount)
                    unavailableReason: !backend.hasKeyData ? qsTr("No shortcuts were recorded.")
                        : qsTr("Shortcut hints need GIF or WebP on this system.")
                    tooltipText: qsTr("Show the shortcuts pressed while recording")
                    selected: backend.keyOverlayAvailable && backend.keyOverlayEnabled
                    onClicked: root.openSection("keys")
                }
            }

            Column {
                id: clipPanel
                objectName: "previewClipPanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: parent.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: root.selectedPartData !== null && !cropOverlay.editing
                FieldGroup {
                    title: qsTr("Source range")
                    Text {
                        width: parent.width
                        text: root.selectedPartData ? qsTr("%1 – %2", "time range")
                            .arg(backend.formatPreciseTime(root.selectedPartData.start))
                            .arg(backend.formatPreciseTime(root.selectedPartData.end)) : ""
                        color: root.textPrimary
                        font.pixelSize: SemanticTokens.fontSizeSmall
                        font.features: { "tnum": 1 }
                    }
                }
                EditorChoice {
                    objectName: "previewPartSpeed"
                    label: qsTr("Export speed")
                    tooltipText: qsTr("How fast this clip plays in the exported file. Changed-speed clips are silent.")
                    choices: root.partSpeeds.map(v => root.speedLabel(v))
                    currentIndex: root.partSpeeds.indexOf(root.selectedPartData ? root.selectedPartData.speed : 1)
                    currentValueText: root.speedLabel(root.selectedPartData ? root.selectedPartData.speed : 1)
                    enabled: backend.timelineEditsAvailable && root.selectedPartData !== null && !root.selectedPartData.deleted
                    onActivated: function(index) {
                        const part = root.selectedPart
                        backend.setPartSpeed(part, root.partSpeeds[index])
                        root.selectedPart = part
                    }
                }
                Text {
                    width: parent.width
                    visible: !backend.timelineEditsAvailable
                    text: qsTr("Cuts and speed changes need GIF or WebP on this system.")
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    wrapMode: Text.WordWrap
                }
                SegmentButton {
                    objectName: "previewClipCut"
                    iconSource: root.iconUrl("trash-2")
                    destructive: !(root.selectedPartData && root.selectedPartData.deleted)
                    text: root.selectedPartData && root.selectedPartData.deleted ? qsTr("Keep clip") : qsTr("Cut clip")
                    tooltipText: qsTr("Leave this clip out of the export, or put it back (Delete)")
                    enabled: backend.timelineEditsAvailable && backend.timelineParts.length > 1
                    onClicked: root.toggleCutTarget()
                }
            }

            // Editing one mask, in the contextual inspector.
            Column {
                id: maskPanel
                objectName: "previewMaskPanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: parent.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: backend.selectedMask >= 0 && !cropOverlay.editing
                readonly property bool wholeRecording: root.selectedMaskData !== null
                    && root.selectedMaskData.start === 0 && root.selectedMaskData.end < 0

                // The fields are shared by all masks: text typed for one and
                // not applied must not reach the next one selected.
                Connections {
                    target: root.previewBackend
                    function onSelectedMaskChanged() {
                        maskStartField.discardEdit()
                        maskEndField.discardEdit()
                    }
                }

                EditorChoice {
                    objectName: "previewMaskStyle"
                    label: qsTr("Style")
                    tooltipText: qsTr("Solid covers with black; pixelate and blur weaken detail, which may still be recoverable")
                    choices: root.maskStyleLabels
                    currentIndex: root.selectedMaskData !== null ? root.selectedMaskData.style : 0
                    onActivated: function(index) { backend.setMaskStyle(backend.selectedMask, index) }
                }
                FieldGroup {
                    title: qsTr("Time range")
                    EditorSwitch {
                        objectName: "previewMaskWhole"
                        text: qsTr("Whole recording")
                        tooltipText: qsTr("Mask for the whole recording, or only for a time range")
                        on: maskPanel.wholeRecording
                        onClicked: {
                            if (maskPanel.wholeRecording) {
                                const start = videoPlayer.position
                                backend.setMaskRange(backend.selectedMask, start,
                                                     Math.min(timelineArea.dur, start + root.newMaskRangeMs))
                            } else {
                                backend.setMaskRange(backend.selectedMask, 0, -1)
                            }
                        }
                    }
                    Row {
                        visible: !maskPanel.wholeRecording
                        spacing: 6
                        TimeField {
                            id: maskStartField
                            objectName: "previewMaskStartField"
                            width: (maskPanel.width - 2 * 6 - 8) / 2
                            accessibleName: qsTr("Mask start")
                            timeMs: root.selectedMaskData ? root.selectedMaskData.start : 0
                            onCommitted: function(ms) {
                                invalid = !backend.setMaskRange(backend.selectedMask, ms, root.selectedMaskData.end)
                            }
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "–"; color: root.textSecondary }
                        TimeField {
                            id: maskEndField
                            objectName: "previewMaskEndField"
                            openEnded: root.selectedMaskData !== null && root.selectedMaskData.end < 0
                            width: (maskPanel.width - 2 * 6 - 8) / 2
                            accessibleName: qsTr("Mask end")
                            timeMs: root.selectedMaskData && root.selectedMaskData.end >= 0 ? root.selectedMaskData.end : timelineArea.dur
                            onCommitted: function(ms) {
                                invalid = !backend.setMaskRange(backend.selectedMask, root.selectedMaskData.start, ms)
                            }
                        }
                    }
                }
                Text {
                    width: parent.width
                    text: qsTr("Drag the mask on the preview to move it, or its corner to resize it.")
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    wrapMode: Text.WordWrap
                }
                SegmentButton {
                    objectName: "previewMaskDelete"
                    quiet: true
                    destructive: true
                    minimumButtonWidth: 0
                    iconSource: root.iconUrl("trash-2")
                    text: qsTr("Delete mask")
                    tooltipText: qsTr("Delete this mask (Delete)")
                    onClicked: backend.removeMask(backend.selectedMask)
                }
            }

            // Project-wide pointer effects.
            Column {
                id: pointerPanel
                objectName: "previewPointerPanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: parent.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: root.selectedPart < 0 && root.pointerOptionsOpen && backend.cursorEffectsAvailable
                         && !(backend.selectedPointerHide >= 0) && !(backend.selectedMask >= 0) && !(backend.selectedOverlay >= 0)
                         && !(backend.selectedZoom >= 0) && !cropOverlay.editing

                EditorSwitch {
                    objectName: "previewPointerToggle"
                    visible: backend.hasPointerData
                    enabled: backend.cursorEffectsAvailable
                    text: qsTr("Pointer")
                    tooltipText: backend.showCursor ? qsTr("Hide the Mouse Pointer") : qsTr("Show the Mouse Pointer")
                    on: backend.showCursor && backend.cursorEffectsAvailable
                    onClicked: backend.showCursor = !backend.showCursor
                }
                EditorChoice {
                    objectName: "previewPointerSize"
                    visible: backend.hasPointerData
                    enabled: backend.cursorEffectsAvailable && backend.showCursor
                    tooltipText: qsTr("Pointer Size: %1").arg(root.speedLabel(backend.cursorScale))
                    label: qsTr("Pointer size")
                    choices: root.pointerScales.map(v => root.speedLabel(v))
                    currentIndex: root.pointerScales.indexOf(backend.cursorScale)
                    currentValueText: root.speedLabel(backend.cursorScale)
                    onActivated: function(index) { backend.cursorScale = root.pointerScales[index] }
                }
                EditorChoice {
                    objectName: "previewPointerSmoothing"
                    visible: backend.hasPointerData
                    enabled: backend.cursorEffectsAvailable && backend.showCursor
                    tooltipText: qsTr("Smooths the pointer's path; clicks stay where they happened")
                    label: qsTr("Smoothing")
                    choices: root.pointerSmoothingLabels
                    currentIndex: backend.cursorSmoothing
                    onActivated: function(index) { backend.cursorSmoothing = index }
                }
                EditorSwitch {
                    objectName: "previewClickRipple"
                    visible: backend.hasPointerData
                    enabled: backend.clickEffectsAvailable
                    text: qsTr("Clicks")
                    tooltipText: backend.clickRipple ? qsTr("Hide Click Highlights") : qsTr("Highlight Clicks")
                    on: backend.clickRipple && backend.clickEffectsAvailable
                    onClicked: backend.clickRipple = !backend.clickRipple
                }
                EditorChoice {
                    objectName: "previewPointerIdle"
                    tooltipText: qsTr("Fade the pointer out when it stops moving; it comes back before it moves or clicks")
                    label: qsTr("Hide when idle")
                    choices: root.pointerIdleChoices.map(v => v === 0 ? qsTr("Off") : qsTr("%1 s").arg(root.secondsLabel(v)))
                    currentIndex: root.pointerIdleChoices.indexOf(backend.cursorIdleHide)
                    currentValueText: qsTr("%1 s").arg(root.secondsLabel(backend.cursorIdleHide))
                    onActivated: function(index) { backend.cursorIdleHide = root.pointerIdleChoices[index] }
                }
                EditorChoice {
                    objectName: "previewPointerBlur"
                    tooltipText: qsTr("A short trail behind a fast-moving pointer")
                    label: qsTr("Motion blur")
                    choices: root.pointerBlurLabels
                    currentIndex: backend.cursorMotionBlur
                    onActivated: function(index) { backend.cursorMotionBlur = index }
                }
                EditorChoice {
                    objectName: "previewPointerStillEnd"
                    tooltipText: qsTr("Keep the pointer still at the end, so moving to stop the recording is not shown")
                    label: qsTr("Still ending")
                    choices: root.pointerStillEndChoices.map(v => v === 0 ? qsTr("Off") : qsTr("%1 s").arg(root.secondsLabel(v)))
                    currentIndex: root.pointerStillEndChoices.indexOf(backend.cursorStillEnd)
                    currentValueText: qsTr("%1 s").arg(root.secondsLabel(backend.cursorStillEnd))
                    onActivated: function(index) { backend.cursorStillEnd = root.pointerStillEndChoices[index] }
                }
                SegmentButton {
                    objectName: "previewPointerHideHere"
                    minimumButtonWidth: 0
                    text: qsTr("Hide Pointer Here")
                    tooltipText: qsTr("Hide the pointer for a while from the playhead")
                    onClicked: {
                        videoPlayer.pause()
                        if (backend.addPointerHideAt(videoPlayer.position) < 0)
                            root.showButtonTooltip(qsTr("No room for a hidden range here"), this, true)
                    }
                }
            }

            // Editing one text or image overlay (#188), in the contextual inspector.
            Column {
                id: overlayPanel
                objectName: "previewOverlayPanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: parent.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: root.selectedOverlayData !== null && !(backend.selectedMask >= 0)
                         && !(backend.selectedZoom >= 0) && !cropOverlay.editing
                readonly property bool isText: root.selectedOverlayData !== null && root.selectedOverlayData.kind === 0
                readonly property bool wholeRecording: root.selectedOverlayData !== null
                    && root.selectedOverlayData.start === 0 && root.selectedOverlayData.end < 0

                Connections {
                    target: root.previewBackend
                    function onSelectedOverlayChanged() {
                        overlayStartField.discardEdit()
                        overlayEndField.discardEdit()
                    }
                }

                FieldGroup {
                    title: qsTr("Text")
                    visible: overlayPanel.isText
                    ScrollView {
                        width: parent.width
                        height: 3 * ComponentTokens.recordingPreviewControlButtonHeight
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        TextArea {
                            id: overlayTextField
                            objectName: "previewOverlayText"
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.Wrap
                            font.pixelSize: SemanticTokens.fontSizeSmall
                            color: root.textPrimary
                            selectByMouse: true
                            placeholderText: qsTr("Text")
                            Accessible.name: qsTr("Overlay text")
                            text: root.selectedOverlayData ? root.selectedOverlayData.text : ""
                            // Applied as typed; one undo step per edit.
                            onTextEdited: backend.setOverlayText(backend.selectedOverlay, text)
                            Keys.onEscapePressed: function(event) {
                                if (root.cancelTimelineDragOnEscape(event)) return
                                root.forceActiveFocus()
                                event.accepted = true
                            }
                            background: Rectangle {
                                radius: SemanticTokens.radiusSmall
                                color: ComponentTokens.editorControl
                                border.width: 1
                                border.color: overlayTextField.activeFocus ? root.accent : root.borderColor
                            }
                        }
                    }
                }
                FieldGroup {
                    title: qsTr("Font and size")
                    visible: overlayPanel.isText
                    SettingsCombo {
                        objectName: "previewOverlayFont"
                        width: parent.width
                        labelWidth: 0
                        controlWidth: parent.width
                        minimumPopupWidth: 260
                        label: ""
                        accessibleName: qsTr("Font")
                        readonly property var families: [""].concat(Qt.fontFamilies())
                        model: families.map(function(family) {
                            return {text: family || qsTr("Default"), value: family}
                        })
                        currentIndex: Math.max(0, families.indexOf(root.selectedOverlayData
                            ? root.selectedOverlayData.fontFamily : ""))
                        onActivated: function(index) {
                            backend.setOverlayFontFamily(backend.selectedOverlay, families[index])
                        }
                    }
                    Row {
                        spacing: 6
                        SegmentButton {
                            objectName: "previewOverlaySmaller"
                            minimumButtonWidth: 44
                            text: "A\u2212"
                            accessibleName: qsTr("Smaller text")
                            tooltipText: qsTr("Smaller")
                            onClicked: backend.setOverlaySize(backend.selectedOverlay, root.selectedOverlayData.size / 1.25)
                        }
                        SegmentButton {
                            objectName: "previewOverlayLarger"
                            minimumButtonWidth: 44
                            text: "A+"
                            accessibleName: qsTr("Larger text")
                            tooltipText: qsTr("Larger")
                            onClicked: backend.setOverlaySize(backend.selectedOverlay, root.selectedOverlayData.size * 1.25)
                        }
                    }
                }
                FieldGroup {
                    title: qsTr("Colour")
                    visible: overlayPanel.isText
                    Flow {
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: overlayPanel.isText ? root.overlayColors : []
                            Rectangle {
                                required property int index
                                required property string modelData
                                objectName: "previewOverlayColor" + index
                                readonly property bool chosen: root.selectedOverlayData !== null
                                    && Qt.colorEqual(root.selectedOverlayData.color, modelData)
                                width: 26
                                height: width
                                radius: width / 2
                                color: modelData
                                border.width: chosen ? 2 : 1
                                border.color: chosen ? root.accent : root.borderColor
                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Text colour: %1").arg(root.overlayColorNames[index])
                                Accessible.checkable: true
                                Accessible.checked: chosen
                                Accessible.onPressAction: backend.setOverlayColor(backend.selectedOverlay, modelData)
                                Keys.onSpacePressed: backend.setOverlayColor(backend.selectedOverlay, modelData)
                                Keys.onReturnPressed: backend.setOverlayColor(backend.selectedOverlay, modelData)
                                FocusFrame { anchors.fill: parent; showFocus: parent.activeFocus }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: CursorTokens.clickable
                                    onClicked: backend.setOverlayColor(backend.selectedOverlay, parent.modelData)
                                }
                            }
                        }
                    }
                }
                EditorChoice {
                    objectName: "previewOverlayBackground"
                    visible: overlayPanel.isText
                    tooltipText: qsTr("A box behind the text, so it reads on any picture")
                    label: qsTr("Background")
                    choices: root.overlayBackgroundLabels
                    currentIndex: root.selectedOverlayData ? root.selectedOverlayData.background : 0
                    onActivated: function(index) { backend.setOverlayBackground(backend.selectedOverlay, index) }
                }
                EditorChoice {
                    objectName: "previewOverlayAlign"
                    visible: overlayPanel.isText
                    tooltipText: qsTr("How lines line up")
                    label: qsTr("Alignment")
                    choices: root.overlayAlignLabels
                    currentIndex: root.selectedOverlayData ? root.selectedOverlayData.align : 0
                    onActivated: function(index) { backend.setOverlayAlign(backend.selectedOverlay, index) }
                }
                EditorChoice {
                    objectName: "previewOverlayOpacity"
                    visible: !overlayPanel.isText
                    tooltipText: qsTr("How see-through the image is")
                    label: qsTr("Opacity")
                    choices: ["100%", "75%", "50%"]
                    currentIndex: [1, 0.75, 0.5].indexOf(root.selectedOverlayData ? root.selectedOverlayData.opacity : 1)
                    currentValueText: qsTr("%1%").arg(Math.round(100 * (root.selectedOverlayData ? root.selectedOverlayData.opacity : 1)))
                    onActivated: function(index) { backend.setOverlayOpacity(backend.selectedOverlay, [1, 0.75, 0.5][index]) }
                }
                FieldGroup {
                    title: qsTr("Time range")
                    EditorSwitch {
                        objectName: "previewOverlayWhole"
                        text: qsTr("Whole recording")
                        tooltipText: qsTr("Show it for the whole recording, or only for a time range")
                        on: overlayPanel.wholeRecording
                        onClicked: {
                            if (overlayPanel.wholeRecording) {
                                const start = Math.min(videoPlayer.position, Math.max(0, timelineArea.dur - 3000))
                                backend.setOverlayRange(backend.selectedOverlay, start, Math.min(timelineArea.dur, start + 3000))
                            } else {
                                backend.setOverlayRange(backend.selectedOverlay, 0, -1)
                            }
                        }
                    }
                    Row {
                        visible: !overlayPanel.wholeRecording
                        spacing: 6
                        TimeField {
                            id: overlayStartField
                            objectName: "previewOverlayStartField"
                            width: (overlayPanel.width - 2 * 6 - 8) / 2
                            accessibleName: qsTr("Overlay start")
                            timeMs: root.selectedOverlayData ? root.selectedOverlayData.start : 0
                            onCommitted: function(ms) {
                                invalid = !backend.setOverlayRange(backend.selectedOverlay, ms, root.selectedOverlayData.end)
                            }
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "–"; color: root.textSecondary }
                        TimeField {
                            id: overlayEndField
                            objectName: "previewOverlayEndField"
                            openEnded: root.selectedOverlayData !== null && root.selectedOverlayData.end < 0
                            width: (overlayPanel.width - 2 * 6 - 8) / 2
                            accessibleName: qsTr("Overlay end")
                            timeMs: root.selectedOverlayData && root.selectedOverlayData.end >= 0 ? root.selectedOverlayData.end : timelineArea.dur
                            onCommitted: function(ms) {
                                invalid = !backend.setOverlayRange(backend.selectedOverlay, root.selectedOverlayData.start, ms)
                            }
                        }
                    }
                }
                SegmentButton {
                    objectName: "previewOverlayDelete"
                    quiet: true
                    destructive: true
                    minimumButtonWidth: 0
                    iconSource: root.iconUrl("trash-2")
                    text: overlayPanel.isText ? qsTr("Delete text") : qsTr("Delete image")
                    tooltipText: qsTr("Delete this overlay (Delete)")
                    onClicked: backend.removeOverlay(backend.selectedOverlay)
                }
            }

            // The two audio tracks (#189), in the contextual inspector.
            Column {
                id: audioPanel
                objectName: "previewAudioPanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: parent.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: root.selectedPart < 0 && root.audioOpen && backend.audioMixAvailable && !cropOverlay.editing
                         && !(backend.selectedPointerHide >= 0) && !(backend.selectedMask >= 0) && !(backend.selectedOverlay >= 0)
                         && !(backend.selectedZoom >= 0)

                EditorChoice {
                    objectName: "previewMicrophoneGain"
                    enabled: !backend.microphoneMuted
                    tooltipText: qsTr("Microphone volume in the export")
                    label: qsTr("Microphone volume")
                    choices: root.audioGainChoices.map(v => root.gainLabel(v))
                    currentIndex: root.audioGainChoices.indexOf(backend.microphoneGainDb)
                    currentValueText: root.gainLabel(backend.microphoneGainDb)
                    onActivated: function(index) { backend.microphoneGainDb = root.audioGainChoices[index] }
                }
                EditorSwitch {
                    objectName: "previewMicrophoneMute"
                    text: qsTr("Mute Microphone")
                    tooltipText: qsTr("Leave the microphone out of the export")
                    on: backend.microphoneMuted
                    onClicked: backend.microphoneMuted = !backend.microphoneMuted
                }
                EditorSwitch {
                    objectName: "previewNoiseReduction"
                    enabled: !backend.microphoneMuted
                    text: qsTr("Reduce Noise")
                    tooltipText: qsTr("Turn down hiss, hum and room noise between words on the microphone")
                    on: backend.microphoneNoiseReduction
                    onClicked: backend.microphoneNoiseReduction = !backend.microphoneNoiseReduction
                }
                EditorChoice {
                    objectName: "previewSystemAudioGain"
                    enabled: !backend.systemAudioMuted
                    tooltipText: qsTr("System audio volume in the export")
                    label: qsTr("System audio volume")
                    choices: root.audioGainChoices.map(v => root.gainLabel(v))
                    currentIndex: root.audioGainChoices.indexOf(backend.systemAudioGainDb)
                    currentValueText: root.gainLabel(backend.systemAudioGainDb)
                    onActivated: function(index) { backend.systemAudioGainDb = root.audioGainChoices[index] }
                }
                EditorSwitch {
                    objectName: "previewSystemAudioMute"
                    text: qsTr("Mute System Audio")
                    tooltipText: qsTr("Leave the system audio out of the export")
                    on: backend.systemAudioMuted
                    onClicked: backend.systemAudioMuted = !backend.systemAudioMuted
                }
            }

            // Shortcut hints (#187), in the contextual inspector.
            Column {
                id: keysPanel
                objectName: "previewKeysPanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: parent.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: root.selectedPart < 0 && root.keysOpen && backend.keyOverlayAvailable && !cropOverlay.editing
                         && !(backend.selectedPointerHide >= 0) && !(backend.selectedMask >= 0) && !(backend.selectedOverlay >= 0)
                         && !(backend.selectedZoom >= 0)

                EditorSwitch {
                    objectName: "previewKeysToggle"
                    text: backend.keyOverlayEnabled ? qsTr("Keys: On") : qsTr("Keys: Off")
                    tooltipText: qsTr("Draw the shortcuts into the exported video")
                    on: backend.keyOverlayEnabled
                    onClicked: backend.keyOverlayEnabled = !backend.keyOverlayEnabled
                }
                EditorChoice {
                    objectName: "previewKeysPosition"
                    tooltipText: qsTr("Where the shortcuts appear")
                    label: qsTr("Position")
                    choices: root.keyPositionLabels
                    currentIndex: backend.keyOverlayPosition
                    onActivated: function(index) { backend.keyOverlayPosition = index }
                }
                EditorChoice {
                    objectName: "previewKeysSize"
                    tooltipText: qsTr("How large the shortcuts are")
                    label: qsTr("Size")
                    choices: root.keySizeLabels
                    currentIndex: backend.keyOverlaySize
                    onActivated: function(index) { backend.keyOverlaySize = index }
                }
                EditorChoice {
                    objectName: "previewKeysStyle"
                    tooltipText: qsTr("Dark or light keys")
                    label: qsTr("Appearance")
                    choices: [qsTr("Dark"), qsTr("Light")]
                    currentIndex: backend.keyOverlayStyle
                    onActivated: function(index) { backend.keyOverlayStyle = index }
                }
                EditorChoice {
                    objectName: "previewKeysDuration"
                    tooltipText: qsTr("How long a shortcut stays after it was pressed; keys pressed close together show together")
                    label: qsTr("Display duration")
                    choices: root.keyDisplayChoices.map(v => qsTr("%1 s").arg(root.secondsLabel(v)))
                    currentIndex: root.keyDisplayChoices.indexOf(backend.keyOverlayDisplayMs)
                    currentValueText: qsTr("%1 s").arg(root.secondsLabel(backend.keyOverlayDisplayMs))
                    onActivated: function(index) { backend.keyOverlayDisplayMs = root.keyDisplayChoices[index] }
                }
            }

            // The frame around the export (#184), in the contextual inspector.
            Column {
                id: appearancePanel
                objectName: "previewAppearancePanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: parent.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: root.selectedPart < 0 && root.appearanceOpen && backend.appearanceAvailable && !cropOverlay.editing
                         && !(backend.selectedPointerHide >= 0) && !(backend.selectedMask >= 0) && !(backend.selectedOverlay >= 0)
                         && !(backend.selectedZoom >= 0)

                EditorSwitch {
                    objectName: "previewAppearanceToggle"
                    text: backend.appearanceEnabled ? qsTr("Frame: On") : qsTr("Frame: Off")
                    tooltipText: qsTr("Draw the frame around the exported video")
                    on: backend.appearanceEnabled
                    onClicked: backend.appearanceEnabled = !backend.appearanceEnabled
                }
                Flow {
                    width: parent.width
                    spacing: 8
                Repeater {
                    model: backend.appearanceGradients
                    Rectangle {
                        required property int index
                        required property var modelData
                        objectName: "previewAppearanceGradient" + index
                        readonly property bool chosen: backend.appearanceEnabled && backend.appearanceBackground === 0
                                                       && backend.appearanceGradientPreset === index
                        width: ComponentTokens.recordingPreviewControlButtonHeight
                        height: width
                        radius: SemanticTokens.radiusSmall
                        border.width: chosen ? 2 : 1
                        border.color: chosen ? root.accent : root.borderColor
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: modelData.start }
                            GradientStop { position: 1; color: modelData.end }
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Background: %1").arg(modelData.name)
                        Accessible.checkable: true
                        Accessible.checked: chosen
                        Accessible.onPressAction: backend.appearanceGradientPreset = index
                        activeFocusOnTab: true
                        Keys.onSpacePressed: backend.appearanceGradientPreset = index
                        Keys.onReturnPressed: backend.appearanceGradientPreset = index
                        FocusFrame { anchors.fill: parent; showFocus: parent.activeFocus }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: CursorTokens.clickable
                            onClicked: backend.appearanceGradientPreset = parent.index
                        }
                    }
                }
                }
                Flow {
                    width: parent.width
                    spacing: 8
                Repeater {
                    model: ["#ffffff", "#000000"]
                    Rectangle {
                        required property int index
                        required property string modelData
                        objectName: "previewAppearanceSolid" + index
                        readonly property bool chosen: backend.appearanceEnabled && backend.appearanceBackground === 1
                                                       && Qt.colorEqual(backend.appearanceSolidColor, modelData)
                        width: ComponentTokens.recordingPreviewControlButtonHeight
                        height: width
                        radius: SemanticTokens.radiusSmall
                        color: modelData
                        border.width: chosen ? 2 : 1
                        border.color: chosen ? root.accent : root.borderColor
                        Accessible.role: Accessible.Button
                        Accessible.name: index === 0 ? qsTr("Background: White") : qsTr("Background: Black")
                        Accessible.checkable: true
                        Accessible.checked: chosen
                        Accessible.onPressAction: backend.appearanceSolidColor = modelData
                        activeFocusOnTab: true
                        Keys.onSpacePressed: backend.appearanceSolidColor = modelData
                        Keys.onReturnPressed: backend.appearanceSolidColor = modelData
                        FocusFrame { anchors.fill: parent; showFocus: parent.activeFocus }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: CursorTokens.clickable
                            onClicked: backend.appearanceSolidColor = parent.modelData
                        }
                    }
                }
                }
                SegmentButton {
                    objectName: "previewAppearanceBlur"
                    minimumButtonWidth: 0
                    text: qsTr("Blurred Video")
                    tooltipText: qsTr("The video itself, blurred, behind it (masks applied)")
                    selected: backend.appearanceEnabled && backend.appearanceBackground === 2
                    exclusive: true
                    onClicked: backend.appearanceBackground = 2
                }
                SegmentButton {
                    objectName: "previewAppearanceImage"
                    minimumButtonWidth: 0
                    text: qsTr("Image…")
                    tooltipText: backend.appearanceImagePath.length > 0
                        ? qsTr("Background image: %1").arg(backend.appearanceImagePath)
                        : qsTr("Use an image file as the background")
                    selected: backend.appearanceEnabled && backend.appearanceBackground === 3
                    exclusive: true
                    onClicked: backend.chooseAppearanceImage()
                }
                // The chosen image, so it is recognisable without opening the file dialog.
                Row {
                    objectName: "previewAppearanceImageChosen"
                    width: parent.width
                    spacing: 8
                    visible: backend.appearanceImagePath.length > 0
                    Rectangle {
                        width: 48
                        height: 30
                        radius: SemanticTokens.radiusSmall
                        color: ComponentTokens.editorCanvas
                        border.width: backend.appearanceEnabled && backend.appearanceBackground === 3 ? 2 : 1
                        border.color: border.width > 1 ? root.accent : root.borderColor
                        clip: true
                        Image {
                            objectName: "previewAppearanceImageThumbnail"
                            anchors.fill: parent
                            anchors.margins: parent.border.width
                            source: backend.appearanceImageUrl
                            sourceSize: Qt.size(96, 60)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            Accessible.role: Accessible.Graphic
                            Accessible.name: qsTr("Background image")
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 56
                        text: backend.appearanceImageName
                        elide: Text.ElideMiddle
                        color: root.textSecondary
                        font.pixelSize: SemanticTokens.fontSizeSmall
                        font.family: SemanticTokens.fontFamily
                    }
                }
                EditorChoice {
                    objectName: "previewAppearancePadding"
                    tooltipText: qsTr("Space around the video")
                    label: qsTr("Padding")
                    choices: root.appearanceAmountLabels
                    currentIndex: backend.appearancePadding
                    onActivated: function(index) { backend.appearancePadding = index }
                }
                EditorChoice {
                    objectName: "previewAppearanceCorners"
                    tooltipText: qsTr("How round the video's corners are")
                    label: qsTr("Corners")
                    choices: root.appearanceAmountLabels
                    currentIndex: backend.appearanceCorners
                    onActivated: function(index) { backend.appearanceCorners = index }
                }
                EditorSwitch {
                    objectName: "previewAppearanceShadow"
                    text: qsTr("Shadow")
                    tooltipText: backend.appearanceShadow ? qsTr("Remove the shadow") : qsTr("Add a shadow under the video")
                    on: backend.appearanceShadow
                    onClicked: backend.appearanceShadow = !backend.appearanceShadow
                }
                EditorSwitch {
                    objectName: "previewAppearanceBorder"
                    text: qsTr("Border")
                    tooltipText: backend.appearanceBorder ? qsTr("Remove the border") : qsTr("Add a thin border around the video")
                    on: backend.appearanceBorder
                    onClicked: backend.appearanceBorder = !backend.appearanceBorder
                }
                EditorChoice {
                    objectName: "previewAppearanceRatio"
                    tooltipText: qsTr("The shape of the exported video; the background fills the extra space")
                    label: qsTr("Aspect ratio")
                    choices: root.appearanceRatioLabels
                    currentIndex: backend.appearanceAspectRatio
                    onActivated: function(index) { backend.appearanceAspectRatio = index }
                }
            }

            // MP4 export options (#190), in the contextual inspector.
            Column {
                id: mp4Panel
                objectName: "previewMp4Panel"
                parent: exportPanel.optionsContent
                width: parent.width
                spacing: SemanticTokens.spacing12
                visible: backend.selectedFormat === 0
                readonly property string sizeOrFpsUnavailable:
                    qsTr("Changing the MP4 size and frame rate is not supported on this system yet")

                EditorChoice {
                    objectName: "previewMp4Resolution"
                    enabled: backend.mp4OutputOptionsAvailable
                    tooltipText: backend.mp4OutputOptionsAvailable
                        ? qsTr("Make the video smaller; it is never made larger than recorded")
                        : mp4Panel.sizeOrFpsUnavailable
                    label: qsTr("Resolution")
                    choices: root.mp4ResolutionLabels
                    currentIndex: backend.mp4Resolution
                    onActivated: function(index) { backend.mp4Resolution = index }
                }
                NumberField {
                    id: mp4WidthField
                    objectName: "previewMp4Width"
                    visible: backend.mp4OutputOptionsAvailable && backend.mp4Resolution === 3
                    accessibleName: qsTr("Video width in pixels")
                    value: backend.mp4CustomWidth
                    onCommitted: function(v) { backend.mp4CustomWidth = v }
                }
                EditorChoice {
                    objectName: "previewMp4FrameRate"
                    enabled: backend.mp4OutputOptionsAvailable
                    tooltipText: backend.mp4OutputOptionsAvailable
                        ? qsTr("A lower frame rate makes a smaller file; it is never higher than recorded")
                        : mp4Panel.sizeOrFpsUnavailable
                    label: qsTr("Frame rate")
                    choices: root.availableMp4FrameRates.map(v => v === 0 ? qsTr("Original") : v + " fps")
                    currentIndex: Math.max(0, root.availableMp4FrameRates.indexOf(backend.mp4FrameRate))
                    onActivated: function(index) { backend.mp4FrameRate = root.availableMp4FrameRates[index] }
                }
                EditorChoice {
                    objectName: "previewMp4Quality"
                    tooltipText: qsTr("Default uses the quality from the Recording settings")
                    label: qsTr("Quality")
                    choices: root.mp4QualityLabels
                    currentIndex: backend.mp4Quality
                    onActivated: function(index) { backend.mp4Quality = index }
                }
                FieldGroup {
                    title: qsTr("Codec")
                    Row {
                        id: codecRow
                        width: parent.width
                        spacing: 6
                        SegmentButton {
                            objectName: "previewMp4H264"
                            minimumButtonWidth: 0
                            width: (codecRow.width - codecRow.spacing) / 2
                            text: "H.264"
                            tooltipText: qsTr("Plays everywhere")
                            selected: backend.mp4Codec === 0 || !backend.mp4HevcAvailable
                            exclusive: true
                            onClicked: backend.mp4Codec = 0
                        }
                        SegmentButton {
                            objectName: "previewMp4Hevc"
                            enabled: backend.mp4HevcAvailable
                            minimumButtonWidth: 0
                            width: (codecRow.width - codecRow.spacing) / 2
                            text: "HEVC"
                            tooltipText: backend.mp4HevcAvailable
                                ? qsTr("Smaller files at the same quality; some older players cannot open them")
                                : Qt.platform.os === "windows"
                                    // The usual reason on Windows: playback needs the Store extension (#210).
                                    ? qsTr("HEVC export needs HEVC Video Extensions from the Microsoft Store")
                                    : qsTr("HEVC export is not available on this system")
                            selected: backend.mp4Codec === 1 && backend.mp4HevcAvailable
                            exclusive: true
                            onClicked: backend.mp4Codec = 1
                        }
                    }
                }
                Text {
                    objectName: "previewMp4Estimate"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    visible: backend.mp4EstimateText.length > 0
                    text: qsTr("%1 × %2, %3 fps · %4 (estimate)")
                        .arg(backend.mp4OutputSize.width).arg(backend.mp4OutputSize.height)
                        .arg(backend.mp4OutputFrameRate).arg(backend.mp4EstimateText)
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

            }

            // Editing one range without a pointer, in the contextual inspector.
            Column {
                id: pointerHidePanel
                objectName: "previewPointerHidePanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: parent.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: backend.selectedPointerHide >= 0 && !(backend.selectedMask >= 0)
                         && !(backend.selectedZoom >= 0) && !cropOverlay.editing

                Connections {
                    target: root.previewBackend
                    function onSelectedPointerHideChanged() {
                        pointerHideStartField.discardEdit()
                        pointerHideEndField.discardEdit()
                    }
                }

                Text {
                    width: parent.width
                    text: qsTr("The pointer is left out of the export during this time.")
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    wrapMode: Text.WordWrap
                }
                FieldGroup {
                    title: qsTr("Time range")
                    Row {
                        spacing: 6
                        TimeField {
                            id: pointerHideStartField
                            objectName: "previewPointerHideStartField"
                            width: (pointerHidePanel.width - 2 * 6 - 8) / 2
                            accessibleName: qsTr("Hidden pointer start")
                            timeMs: root.selectedPointerHideData ? root.selectedPointerHideData.start : 0
                            onCommitted: function(ms) {
                                invalid = !backend.setPointerHideRange(backend.selectedPointerHide, ms,
                                                                       root.selectedPointerHideData.end)
                            }
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "–"; color: root.textSecondary }
                        TimeField {
                            id: pointerHideEndField
                            objectName: "previewPointerHideEndField"
                            width: (pointerHidePanel.width - 2 * 6 - 8) / 2
                            accessibleName: qsTr("Hidden pointer end")
                            timeMs: root.selectedPointerHideData ? root.selectedPointerHideData.end : 0
                            onCommitted: function(ms) {
                                invalid = !backend.setPointerHideRange(backend.selectedPointerHide,
                                                                       root.selectedPointerHideData.start, ms)
                            }
                        }
                    }
                }
                SegmentButton {
                    objectName: "previewPointerHideDelete"
                    quiet: true
                    destructive: true
                    minimumButtonWidth: 0
                    iconSource: root.iconUrl("trash-2")
                    text: qsTr("Show the pointer again")
                    tooltipText: qsTr("Delete this hidden range (Delete)")
                    onClicked: backend.removePointerHide(backend.selectedPointerHide)
                }
            }

            // Editing one zoom segment, in the contextual inspector.
            Column {
                id: zoomPanel
                objectName: "previewZoomPanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: parent.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: backend.selectedZoom >= 0 && !(backend.selectedMask >= 0) && !cropOverlay.editing
                readonly property var segment: backend.selectedZoom >= 0 ? backend.zoomSegments[backend.selectedZoom] : null

                // The fields are shared by all zooms: text typed for one and
                // not applied must not reach the next one selected (#176 review).
                Connections {
                    target: root.previewBackend
                    function onSelectedZoomChanged() {
                        zoomStartField.discardEdit()
                        zoomEndField.discardEdit()
                    }
                }

                FieldGroup {
                    title: qsTr("Time range")
                    Row {
                        spacing: 6
                        TimeField {
                            id: zoomStartField
                            objectName: "previewZoomStartField"
                            width: (zoomPanel.width - 2 * 6 - 8) / 2
                            accessibleName: qsTr("Zoom start")
                            timeMs: zoomPanel.segment ? zoomPanel.segment.start : 0
                            onCommitted: function(ms) {
                                invalid = !backend.setZoomRange(backend.selectedZoom, ms, zoomPanel.segment.end)
                            }
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "–"; color: root.textSecondary }
                        TimeField {
                            id: zoomEndField
                            objectName: "previewZoomEndField"
                            width: (zoomPanel.width - 2 * 6 - 8) / 2
                            accessibleName: qsTr("Zoom end")
                            timeMs: zoomPanel.segment ? zoomPanel.segment.end : 0
                            onCommitted: function(ms) {
                                invalid = !backend.setZoomRange(backend.selectedZoom, zoomPanel.segment.start, ms)
                            }
                        }
                    }
                }
                FieldGroup {
                    title: qsTr("Zoom amount")
                    Row {
                        id: zoomScaleRow
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: root.zoomScales
                            SegmentButton {
                                required property int index
                                required property real modelData
                                objectName: "previewZoomScale" + index
                                minimumButtonWidth: 0
                                width: (zoomScaleRow.width - (root.zoomScales.length - 1) * zoomScaleRow.spacing) / root.zoomScales.length
                                text: modelData.toLocaleString(Qt.locale(), "f", modelData % 1 ? 1 : 0) + "×"
                                tooltipText: qsTr("Zoom: %1").arg(text)
                                selected: zoomPanel.segment !== null && Math.abs(zoomPanel.segment.scale - modelData) < 0.05
                                exclusive: true
                                onClicked: backend.setZoomScale(backend.selectedZoom, modelData)
                            }
                        }
                    }
                }
                FieldGroup {
                    title: qsTr("All zooms")
                    EditorSwitch {
                        objectName: "previewZoomEnabled"
                        text: qsTr("Zoom effects")
                        tooltipText: qsTr("Turn all zooms on or off without deleting them")
                        on: backend.zoomEnabled
                        onClicked: backend.zoomEnabled = !backend.zoomEnabled
                    }
                    EditorSwitch {
                        objectName: "previewZoomReduceMotion"
                        text: qsTr("Less motion")
                        tooltipText: qsTr("Gentler zooms for all segments: smaller and slower, no direct pans")
                        on: backend.zoomReduceMotion
                        onClicked: backend.zoomReduceMotion = !backend.zoomReduceMotion
                    }
                }
                SegmentButton {
                    objectName: "previewZoomDelete"
                    quiet: true
                    destructive: true
                    minimumButtonWidth: 0
                    iconSource: root.iconUrl("trash-2")
                    text: qsTr("Delete zoom")
                    tooltipText: qsTr("Delete this zoom (Delete)")
                    onClicked: backend.removeZoom(backend.selectedZoom)
                }
            }

            // Exact crop entry while editing, in the contextual inspector so the
            // video keeps its size: aspect presets and X/Y/W/H in video pixels.
            Column {
                id: cropPanel
                objectName: "previewCropPanel"
                x: SemanticTokens.spacing16
                y: SemanticTokens.spacing16
                width: controlRowArea.width - 2 * SemanticTokens.spacing16
                spacing: SemanticTokens.spacing16
                visible: cropOverlay.editing

                FieldGroup {
                    title: qsTr("Aspect ratio")
                    Grid {
                        id: aspectGrid
                        width: parent.width
                        columns: 3
                        spacing: 6
                    Repeater {
                        model: root.aspectPresetLabels
                        SegmentButton {
                            required property int index
                            required property string modelData
                            objectName: "previewAspectPreset" + index
                            minimumButtonWidth: 0
                            width: (aspectGrid.width - 2 * aspectGrid.spacing) / 3
                            text: modelData
                            tooltipText: qsTr("Crop aspect: %1").arg(modelData)
                            selected: backend.cropAspectIndex === index
                            exclusive: true
                            onClicked: root.chooseAspect(index)
                        }
                    }
                    }
                }

                FieldGroup {
                    title: qsTr("Position and size (video pixels)")
                    Grid {
                        id: cropFieldGrid
                        width: parent.width
                        columns: 2
                        columnSpacing: SemanticTokens.spacing12
                        rowSpacing: 6
                Repeater {
                    id: cropFieldRepeater
                    model: [
                        { key: "x", label: qsTr("X") },
                        { key: "y", label: qsTr("Y") },
                        { key: "width", label: qsTr("W") },
                        { key: "height", label: qsTr("H") }
                    ]
                    delegate: Row {
                        required property var modelData
                        width: (cropFieldGrid.width - cropFieldGrid.columnSpacing) / 2
                        spacing: 6
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14
                            text: modelData.label
                            color: root.textSecondary
                            font.pixelSize: SemanticTokens.fontSizeCaption
                            font.family: SemanticTokens.fontFamily
                        }
                        property alias field: cropNumberField
                        NumberField {
                            id: cropNumberField
                            objectName: "previewCrop_" + modelData.key
                            width: parent.width - 20
                            accessibleName: qsTr("Crop %1 in pixels").arg(modelData.label)
                            value: root.draftVideoRect[modelData.key]
                            onCommitted: function(v) {
                                const r = root.draftVideoRect
                                let w = modelData.key === "width" ? v : r.width
                                let h = modelData.key === "height" ? v : r.height
                                // Locked ratio: the other side follows, so W or H can
                                // enlarge the crop instead of only moving it (#167 review).
                                // It gets one even step of slack, so the legal size is
                                // limited by the typed side and keeps its value.
                                const ratio = backend.cropAspectRatio
                                if (ratio > 0 && modelData.key === "width")
                                    h = 2 * Math.ceil(w / ratio / 2) + 2
                                else if (ratio > 0 && modelData.key === "height")
                                    w = 2 * Math.ceil(h * ratio / 2) + 2
                                root.setDraftFromFields(modelData.key === "x" ? v : r.x,
                                                        modelData.key === "y" ? v : r.y, w, h)
                            }
                        }
                    }
                }
                    }
                }
            }
        }

    }

    RecordingExportPanel {
        id: exportPanel
        parent: root
        showOptions: backend.selectedFormat === 0
        onExportRequested: root.saveWithCrop()
        onReturnRequested: {
            if (!mp4WidthField.visible || mp4WidthField.commitText()) exportPanel.close()
        }
        // What Save will produce: encoded pixel sizes (not window sizes), kept time, audio.
        summaryText: {
            if (backend.videoSize.width <= 0)
                return ""
            const source = backend.videoSize.width + " × " + backend.videoSize.height
            // What is saved: the crop, a frame's padding and the MP4 Size option (#210).
            const saved = backend.exportSize
            const out = saved.width > 0 ? saved.width + " × " + saved.height : source
            const parts = [qsTr("Original %1 → Output %2").arg(source).arg(out),
                           qsTr("%1 kept").arg(backend.formatPreciseTime(backend.trimmedDuration))]
            if (backend.selectedFormat !== 0)
                parts.push(qsTr("no audio"))
            if (backend.hasPointerData && !backend.cursorEffectsAvailable)
                parts.push(qsTr("pointer effects need GIF or WebP on this system"))
            if (backend.masks.length > 0)
                parts.push(backend.masksAvailable
                           ? qsTr("masks: %1, burned into the export; the unmasked temporary file is deleted after saving").arg(backend.masks.length)
                           : qsTr("masks need GIF or WebP on this system"))
            return parts.join(" · ")
        }
        footerText: backend.selectedFormat === 0 && backend.mp4EstimateText.length > 0
            ? backend.mp4EstimateText : ""

        Column {
            parent: exportPanel.formatContent
            width: parent.width
            spacing: 6
            EditorChoice {
                objectName: "previewExportFormat"
                label: qsTr("Format")
                choices: ["MP4", "GIF", "WebP"]
                currentIndex: backend.selectedFormat
                onActivated: function(index) { backend.selectedFormat = index }
            }
            Text {
                width: parent.width
                text: [qsTr("Video with sound. Best for sharing and long recordings."),
                       qsTr("Silent animation that plays almost anywhere. Large files."),
                       qsTr("Silent animation, smaller than GIF. Supports every edit.")][backend.selectedFormat] || ""
                color: root.textSecondary
                font.pixelSize: SemanticTokens.fontSizeCaption
                wrapMode: Text.WordWrap
            }
        }
    }

    // The preview's controls over the stage, outside its frame-fitting
    // transform: they stay where the window is, not where the framed
    // picture is scaled to (PR #198 review).
    Item {
        id: stageChrome
        parent: editorShell.workspace
        objectName: "previewStageChrome"
        z: 2
        x: stage.x
        y: stage.y
        width: stage.width
        height: stage.height
        enabled: !backend.isProcessing

        Rectangle {
            id: playOverlay
            objectName: "previewPlayOverlay"
            anchors.centerIn: parent
            width: 72
            height: 72
            radius: 36
            color: ComponentTokens.recordingPreviewPlayOverlay
            visible: !videoPlayer.playing
            opacity: visible ? 1.0 : 0.0

            Behavior on opacity {
                NumberAnimation { duration: SemanticTokens.durationInteractionHover }
            }

            SvgIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/icons/play.svg"
                iconSize: ComponentTokens.recordingPreviewActionIconSize
                color: ComponentTokens.recordingPreviewPrimaryButtonIcon
            }
        }

        // Committed crop chip; kept off the toolbar so it fits the minimum window width.
        GlassSurface {
            id: cropChip
            objectName: "previewCropChip"
            visible: backend.hasCrop && !cropOverlay.editing && !root.comparingOriginal
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: SemanticTokens.spacing12
            width: cropChipRow.implicitWidth + SemanticTokens.spacing16
            height: root.overlayChipHeight
            z: 5
            glassBg: ComponentTokens.tooltipBackground
            glassBgTop: ComponentTokens.tooltipBackgroundTop
            glassHighlight: ComponentTokens.tooltipHighlight
            glassBorder: ComponentTokens.tooltipBorder
            glassRadius: ComponentTokens.tooltipRadius

            Row {
                id: cropChipRow
                anchors.centerIn: parent
                spacing: SemanticTokens.spacing4

                Text {
                    objectName: "previewCropSizeLabel"
                    anchors.verticalCenter: parent.verticalCenter
                    // Authoritative: the backend's normalized, even video-pixel crop.
                    text: backend.cropRect.width + " × " + backend.cropRect.height
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                }

                IconButton {
                    objectName: "previewClearCropButton"
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: !backend.isProcessing
                    iconSource: "qrc:/icons/icons/close.svg"
                    iconSize: ComponentTokens.iconSizeMenu
                    width: root.overlayChipButtonSize
                    height: root.overlayChipButtonSize
                    tooltipText: qsTr("Clear Crop")
                    tooltipPreferredAbove: false
                    onClicked: backend.clearCrop()
                }
            }
        }

        GlassSurface {
            objectName: "previewCompareChip"
            visible: root.comparingOriginal
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: SemanticTokens.spacing12
            width: compareText.implicitWidth + SemanticTokens.spacing16
            height: root.overlayChipHeight
            z: 5
            glassBg: ComponentTokens.tooltipBackground
            glassBgTop: ComponentTokens.tooltipBackgroundTop
            glassHighlight: ComponentTokens.tooltipHighlight
            glassBorder: ComponentTokens.tooltipBorder
            glassRadius: ComponentTokens.tooltipRadius

            Text {
                id: compareText
                anchors.centerIn: parent
                text: qsTr("Original")
                color: root.textPrimary
                font.pixelSize: SemanticTokens.fontSizeCaption
                font.family: SemanticTokens.fontFamily
            }
        }

        GlassSurface {
            objectName: "previewWindowSnapHint"
            visible: cropOverlay.editing && backend.hasWindowTimeline && !cropOverlay.hasShownRect
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.margins: SemanticTokens.spacing12
            width: snapHintText.implicitWidth + SemanticTokens.spacing16
            height: root.overlayChipHeight
            z: 5
            glassBg: ComponentTokens.tooltipBackground
            glassBgTop: ComponentTokens.tooltipBackgroundTop
            glassHighlight: ComponentTokens.tooltipHighlight
            glassBorder: ComponentTokens.tooltipBorder
            glassRadius: ComponentTokens.tooltipRadius

            Text {
                id: snapHintText
                anchors.centerIn: parent
                text: qsTr("Click a window to crop to it, or drag to draw")
                color: root.textSecondary
                font.pixelSize: SemanticTokens.fontSizeCaption
                font.family: SemanticTokens.fontFamily
            }
        }

        GlassSurface {
            id: silentPreviewNotice
            objectName: "previewAudioUnavailableLabel"
            visible: !videoPlayer.audioPlaybackSupported
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: SemanticTokens.spacing12
            width: silentPreviewText.implicitWidth + SemanticTokens.spacing16
            height: root.overlayChipHeight
            z: 5
            glassBg: ComponentTokens.tooltipBackground
            glassBgTop: ComponentTokens.tooltipBackgroundTop
            glassHighlight: ComponentTokens.tooltipHighlight
            glassBorder: ComponentTokens.tooltipBorder
            glassRadius: ComponentTokens.tooltipRadius

            Text {
                id: silentPreviewText
                anchors.centerIn: parent
                text: qsTr("Audio playback is unavailable in this preview.")
                font.pixelSize: SemanticTokens.fontSizeCaption
                font.family: SemanticTokens.fontFamily
                color: root.textSecondary
            }
        }
    }

    Rectangle {
        id: processingOverlay
        anchors.fill: parent
        color: SemanticTokens.backgroundOverlay
        visible: backend.isProcessing

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function(wheel) { wheel.accepted = true }
        }

        // The status is read on a solid panel, like the close confirmation:
        // straight on the dimmed overlay its contrast depended on the frame
        // behind it (#220).
        Rectangle {
            objectName: "previewProcessingCard"
            anchors.centerIn: parent
            width: Math.min(parent.width - SemanticTokens.spacing40,
                            Math.max(320, processingColumn.implicitWidth + SemanticTokens.spacing40))
            height: processingColumn.implicitHeight + SemanticTokens.spacing40
            radius: SemanticTokens.radiusMedium
            color: root.bgPanel
            border.width: 1
            border.color: root.borderColor

            Column {
                id: processingColumn
                anchors.centerIn: parent
                spacing: SemanticTokens.spacing16

                Rectangle {
                    width: 280
                    height: 6
                    radius: 3
                    color: root.borderColor
                    anchors.horizontalCenter: parent.horizontalCenter

                    Rectangle {
                        visible: !backend.processIndeterminate
                        width: parent.width * (backend.processProgress / 100.0)
                        height: parent.height
                        radius: 3
                        color: root.accent

                        Behavior on width {
                            NumberAnimation { duration: SemanticTokens.durationInteractionHover }
                        }
                    }

                    // No trustworthy total (writing or saving the file): a moving
                    // segment instead of an invented percentage.
                    Rectangle {
                        id: indeterminateSegment
                        objectName: "previewIndeterminateProgress"
                        visible: backend.processIndeterminate
                        width: parent.width / 4
                        height: parent.height
                        radius: 3
                        color: root.accent
                        SequentialAnimation on x {
                            running: indeterminateSegment.visible
                            loops: Animation.Infinite
                            NumberAnimation {
                                from: 0
                                to: 280 - 280 / 4
                                duration: SemanticTokens.durationNormal * 4
                                easing.type: Easing.InOutQuad
                            }
                            NumberAnimation {
                                from: 280 - 280 / 4
                                to: 0
                                duration: SemanticTokens.durationNormal * 4
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }
                }

                Text {
                    objectName: "previewProcessingStatus"
                    text: backend.processStatus
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    color: root.textPrimary
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Text {
                    visible: !backend.processIndeterminate
                    text: backend.processProgress + "%"
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                    color: root.textSecondary
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                // MP4, GIF and WebP exports stop between frames; the recording and
                // edits stay, so the user can change settings and save again.
                DialogButton {
                    objectName: "previewCancelExportButton"
                    text: qsTr("Cancel")
                    style: "secondary"
                    visible: backend.canCancelExport && !backend.exportCloseQuestionPending
                    anchors.horizontalCenter: parent.horizontalCenter
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Cancel Export")
                    onClicked: backend.cancelExport()
                }

                // The window was closed during an export: keep it or cancel it.
                Column {
                    objectName: "previewExportCloseQuestion"
                    visible: backend.exportCloseQuestionPending
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: SemanticTokens.spacing8

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("An export is in progress. Keep exporting or cancel it?")
                        font.pixelSize: SemanticTokens.fontSizeBody
                        font.family: SemanticTokens.fontFamily
                        color: root.textPrimary
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: SemanticTokens.spacing8
                        DialogButton {
                            objectName: "previewKeepExportingButton"
                            text: qsTr("Keep Exporting")
                            style: "primary"
                            Accessible.role: Accessible.Button
                            Accessible.name: text
                            onClicked: backend.keepExporting()
                        }
                        DialogButton {
                            objectName: "previewCancelExportFromCloseButton"
                            text: qsTr("Cancel Export")
                            style: "secondary"
                            Accessible.role: Accessible.Button
                            Accessible.name: text
                            onClicked: backend.cancelExportFromClose()
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: discardConfirmation
        objectName: "discardConfirmation"
        anchors.fill: parent
        color: SemanticTokens.backgroundOverlay
        visible: backend.discardConfirmationPending === true
        z: 30

        // 0 = Keep Recording (default), 1 = Discard
        property int focusIndex: 0
        // The editor that had keyboard focus: the confirmation takes the keys
        // while open (a focused field would otherwise commit on Enter or
        // restore on Escape behind it) and hands focus back on close.
        property Item focusBeforeOpen: null
        onVisibleChanged: {
            if (visible) {
                focusIndex = 0
                // Popups sit in the window overlay, above this confirmation: close
                // them so the confirmation is what is seen and what takes the keys
                // (PR #228 review). Focus goes back to where editing was.
                const exportOpen = exportPanel.opened
                const current = exportOpen ? exportPanel.returnFocusItem : root.Window.activeFocusItem
                exportPanel.returnFocusItem = null
                exportPanel.close()
                effectMenu.close()
                focusBeforeOpen = current && current !== root ? current : null
                root.forceActiveFocus()
            } else {
                const previous = focusBeforeOpen
                focusBeforeOpen = null
                if (previous && previous.visible && previous.enabled)
                    previous.forceActiveFocus()
            }
        }

        function activateFocused() {
            if (focusIndex === 1)
                backend.discard()
            else
                backend.cancelDiscard()
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function(wheel) { wheel.accepted = true }
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - SemanticTokens.spacing40, 380)
            height: confirmColumn.implicitHeight + SemanticTokens.spacing40
            radius: SemanticTokens.radiusMedium
            color: root.bgPanel
            border.width: 1
            border.color: root.borderColor

            Accessible.role: Accessible.AlertMessage
            Accessible.name: confirmTitle.text
            Accessible.description: confirmBody.text

            Column {
                id: confirmColumn
                anchors.centerIn: parent
                width: parent.width - SemanticTokens.spacing40
                spacing: SemanticTokens.spacing12

                Text {
                    id: confirmTitle
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: backend.isProject ? qsTr("Close without saving your changes?") : qsTr("Discard this recording?")
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    font.weight: SemanticTokens.fontWeightMedium
                    color: root.textPrimary
                }

                Text {
                    id: confirmBody
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: backend.isProject
                        ? qsTr("The project stays in Recording Library as it was last saved.")
                        : qsTr("The video will be deleted and cannot be recovered.")
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                    color: root.textSecondary
                }

                Row {
                    anchors.right: parent.right
                    spacing: SemanticTokens.spacing8

                    DialogButton {
                        objectName: "discardKeepButton"
                        text: backend.isProject ? qsTr("Keep Editing") : qsTr("Keep Recording")
                        style: "secondary"
                        border.width: discardConfirmation.focusIndex === 0 ? 2 : 1
                        border.color: discardConfirmation.focusIndex === 0 ? root.accent
                                                                           : ComponentTokens.buttonSecondaryBorder
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                        Accessible.onPressAction: backend.cancelDiscard()
                        onClicked: backend.cancelDiscard()
                    }

                    DialogButton {
                        objectName: "discardConfirmButton"
                        text: backend.isProject ? qsTr("Close") : qsTr("Discard")
                        style: "primary"
                        color: ComponentTokens.recordingPreviewDangerPressed
                        border.width: discardConfirmation.focusIndex === 1 ? 2 : 0
                        border.color: root.textPrimary
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                        Accessible.onPressAction: backend.discard()
                        onClicked: backend.discard()
                    }
                }
            }
        }
    }

    Rectangle {
        id: errorBanner
        objectName: "previewErrorBanner"
        // Over the top of the workspace: never over the top bar or the inspector.
        parent: editorShell.workspace
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: SemanticTokens.spacing12
        height: visible ? errorRow.implicitHeight + SemanticTokens.spacing16 : 0
        visible: backend.errorMessage !== ""
        radius: SemanticTokens.radiusSmall
        // Solid, so it reads over any picture.
        color: ComponentTokens.editorSurface
        border.width: 1
        border.color: ComponentTokens.editorDestructiveText
        z: 10

        Row {
            id: errorRow
            anchors.centerIn: parent
            width: parent.width - SemanticTokens.spacing16
            spacing: SemanticTokens.spacing8

            Text {
                // Leaves room for the dismiss button and, when shown, Try Again.
                width: parent.width - 28 - (retryButtonSlot.visible ? retryButtonSlot.width + errorRow.spacing : 0)
                wrapMode: Text.Wrap
                text: backend.errorMessage
                font.pixelSize: SemanticTokens.fontSizeBody
                font.family: SemanticTokens.fontFamily
                color: ComponentTokens.editorDestructiveText
                anchors.verticalCenter: parent.verticalCenter
            }

            IconButton {
                id: retryButtonSlot
                objectName: "previewRetrySaveButton"
                visible: backend.canRetrySave
                iconText: qsTr("Try Again")
                height: 20
                tooltipText: qsTr("Save again with the current settings")
                tooltipPreferredAbove: false
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    backend.clearError()
                    root.openExport()
                }
            }

            IconButton {
                iconSource: "qrc:/icons/icons/close.svg"
                iconSize: ComponentTokens.iconSizeMenu
                width: 20
                height: 20
                tooltipText: qsTr("Dismiss Error")
                tooltipPreferredAbove: false
                anchors.verticalCenter: parent.verticalCenter
                onClicked: backend.clearError()
            }
        }
    }

    focus: true

    Keys.onPressed: function(event) {
        if (backend.isProcessing) {
            event.accepted = true
            return
        }
        if (backend.discardConfirmationPending) {
            // Esc always keeps the recording; Tab/arrows move between the two
            // buttons and Enter/Space activates the focused one ("Keep" first).
            switch (event.key) {
            case Qt.Key_Escape:
                backend.cancelDiscard()
                break
            case Qt.Key_Tab:
            case Qt.Key_Backtab:
            case Qt.Key_Left:
            case Qt.Key_Right:
                discardConfirmation.focusIndex = 1 - discardConfirmation.focusIndex
                break
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Space:
                discardConfirmation.activateFocused()
                break
            }
            event.accepted = true
            return
        }

        // Unhandled popup keys must never seek, edit, or discard the workspace.
        if (exportPanel.opened) {
            if (event.key === Qt.Key_Escape) exportPanel.returnRequested()
            event.accepted = true
            return
        }

        // Keys a text field did not use (for example Left at its first
        // character) must not seek, play or discard.
        if (root.textEntryActive)
            return

        // A drag in progress is cancelled by Escape before anything else:
        // never by closing the editor, nor by dropping a crop draft.
        if (event.key === Qt.Key_Escape && root.cancelTimelineDragOnEscape(event))
            return

        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z) {
            if (event.modifiers & Qt.ShiftModifier)
                root.redoEdit()
            else
                root.undoEdit()
            event.accepted = true
            return
        }
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Y) {
            root.redoEdit()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Backslash && backend.hasCrop) {
            root.comparingOriginal = !root.comparingOriginal
            event.accepted = true
            return
        }

        if (cropOverlay.editing) {
            // Crops sit on even pixels (encoder requirement), so 2 px is the smallest step.
            const step = (event.modifiers & Qt.ShiftModifier) ? 10 : 2
            const nudges = {}
            nudges[Qt.Key_Left] = [-step, 0]
            nudges[Qt.Key_Right] = [step, 0]
            nudges[Qt.Key_Up] = [0, -step]
            nudges[Qt.Key_Down] = [0, step]
            if (nudges[event.key] !== undefined) {
                root.nudgeDraft(nudges[event.key][0], nudges[event.key][1])
                event.accepted = true
                return
            }
            // Escape only drops the draft (the preview stays open); Enter only applies it.
            if (event.key === Qt.Key_Escape) {
                cropOverlay.cancel()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.applyCropWithPendingInput()
                event.accepted = true
                return
            }
        }

        switch (event.key) {
        case Qt.Key_Space:
            videoPlayer.togglePlayPause()
            event.accepted = true
            break
        case Qt.Key_Escape:
            // (A drag in progress was cancelled above.)
            if (root.inspectorKind !== "project") {
                if (root.openSection("") && !root.wideEditor)
                    root.inspectorOpen = false
            }
            else if (!root.wideEditor && root.inspectorOpen)
                root.inspectorOpen = false
            else
                backend.requestDiscard()
            event.accepted = true
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.openExport()
            event.accepted = true
            break
        case Qt.Key_M:
            if (videoPlayer.audioPlaybackSupported) {
                videoPlayer.muted = !videoPlayer.muted
                event.accepted = true
            }
            break
        case Qt.Key_Left:
            videoPlayer.seek(Math.max(0, videoPlayer.position - 5000))
            event.accepted = true
            break
        case Qt.Key_Right:
            videoPlayer.seek(Math.min(videoPlayer.duration, videoPlayer.position + 5000))
            event.accepted = true
            break
        case Qt.Key_Comma:
            videoPlayer.pause()
            videoPlayer.seek(Math.max(0, videoPlayer.position - videoPlayer.frameIntervalMs))
            event.accepted = true
            break
        case Qt.Key_Period:
            videoPlayer.stepForward()
            event.accepted = true
            break
        case Qt.Key_S:
            if (event.modifiers & Qt.ControlModifier) {
                root.openExport()
                event.accepted = true
            } else if (!event.modifiers && backend.timelineEditsAvailable) {
                videoPlayer.pause()
                backend.splitAt(videoPlayer.position)
                event.accepted = true
            }
            break
        case Qt.Key_Delete:
        case Qt.Key_Backspace:
            if (backend.selectedMask >= 0 || backend.selectedZoom >= 0
                    || backend.selectedOverlay >= 0 || backend.selectedPointerHide >= 0) {
                if (!root.commitInspector()) { event.accepted = true; break }
                if (backend.selectedMask >= 0) backend.removeMask(backend.selectedMask)
                else if (backend.selectedZoom >= 0) backend.removeZoom(backend.selectedZoom)
                else if (backend.selectedOverlay >= 0) backend.removeOverlay(backend.selectedOverlay)
                else backend.removePointerHide(backend.selectedPointerHide)
                event.accepted = true
            } else if (root.toggleCutTarget()) {
                event.accepted = true
            }
            break
        case Qt.Key_BracketLeft:
            if (root.speedIndex > 0) {
                root.speedIndex--
                root.applyPlaybackRate()
            }
            event.accepted = true
            break
        case Qt.Key_BracketRight:
            if (root.speedIndex < root.speedOptions.length - 1) {
                root.speedIndex++
                root.applyPlaybackRate()
            }
            event.accepted = true
            break
        }
    }

    // Integer entry committed on Enter or focus loss; Esc restores the value
    // and gives focus back to the preview.
    component NumberField: TextField {
        id: numberField
        property int value: 0
        property string accessibleName: ""
        signal committed(int value)

        width: 92
        height: 30
        padding: 4
        rightPadding: 8
        horizontalAlignment: TextInput.AlignRight
        verticalAlignment: TextInput.AlignVCenter
        font.pixelSize: SemanticTokens.fontSizeSmall
        color: root.textPrimary
        selectByMouse: true
        validator: IntValidator { bottom: 0; top: 100000 }
        text: value
        Accessible.name: accessibleName
        background: Rectangle {
            radius: SemanticTokens.radiusSmall
            color: ComponentTokens.editorControl
            border.width: 1
            border.color: numberField.activeFocus ? root.accent : root.borderColor
        }
        // Typed and not applied yet (see TimeField).
        property bool edited: false
        onValueChanged: if (!edited) text = value
        onTextEdited: edited = true
        function hasPendingInput() {
            return edited
        }
        // Applies typed digits; false when they are not a valid number.
        function commitText() {
            if (!edited)
                return true
            if (!acceptableInput)
                return false
            edited = false
            if (parseInt(text) !== value)
                committed(parseInt(text))
            text = Qt.binding(function() { return numberField.value })
            return true
        }
        // Not when the discard confirmation took the focus: typed text stays
        // pending (focus returns here if the recording is kept).
        onEditingFinished: if (!backend.discardConfirmationPending) commitText()
        Keys.onEscapePressed: function(event) {
            if (root.cancelTimelineDragOnEscape(event)) return
            edited = false
            text = Qt.binding(function() { return numberField.value })
            if (exportPanel.opened) exportPanel.contentItem.forceActiveFocus()
            else root.forceActiveFocus()
            event.accepted = true
        }
    }

    // Time entry ("m:ss.mmm"). committed returns false to reject the value.
    component TimeField: TextField {
        id: timeField
        property real timeMs: 0
        // Shows the duration for an open end (-1) that must stay open unless edited.
        property bool openEnded: false
        property string accessibleName: ""
        property bool invalid: false
        signal committed(real ms)

        width: 92
        height: 30
        padding: 4
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        font.pixelSize: SemanticTokens.fontSizeSmall
        color: invalid ? SemanticTokens.statusError : root.textPrimary
        selectByMouse: true
        text: backend.formatPreciseTime(timeMs)
        Accessible.name: accessibleName
        background: Rectangle {
            radius: SemanticTokens.radiusSmall
            color: ComponentTokens.editorControl
            border.width: 1
            border.color: timeField.invalid ? SemanticTokens.statusError
                                            : timeField.activeFocus ? root.accent : root.borderColor
        }
        // Typed by the user and not applied yet. Without it the field always
        // shows the value, even while focused: undo/redo or a handle drag
        // must never leave stale text that Save would apply again (#167 review).
        property bool edited: false
        onTimeMsChanged: if (!edited) text = backend.formatPreciseTime(timeMs)
        function hasPendingText() {
            return edited
        }
        // Applies the typed time; false when it is invalid or rejected.
        function commitText() {
            if (!edited)
                return true
            const ms = backend.parseTime(text)
            invalid = ms < 0
            if (!invalid && ms !== timeMs) {
                committed(ms)
                // The backend rejects out-of-range or too-short ranges and keeps its value.
                invalid = timeMs !== ms
            }
            if (!invalid) {
                edited = false
                text = backend.formatPreciseTime(timeMs)
            }
            return !invalid
        }
        // Not when the discard confirmation took the focus: typed text stays
        // pending (focus returns here if the recording is kept).
        onEditingFinished: if (!backend.discardConfirmationPending) commitText()
        onTextEdited: {
            invalid = false
            edited = true
        }
        // Drops typed text that was not applied, back to the value.
        function discardEdit() {
            invalid = false
            edited = false
            text = backend.formatPreciseTime(timeMs)
        }
        Keys.onEscapePressed: function(event) {
            if (root.cancelTimelineDragOnEscape(event)) return
            discardEdit()
            root.forceActiveFocus()
            event.accepted = true
        }
    }

    component IconButton: Rectangle {
        property url iconSource: ""
        property string iconText: ""
        property string tooltipText: ""
        property bool tooltipPreferredAbove: true
        property bool highlighted: false
        property bool destructive: false
        property bool primary: false
        property int iconSize: ComponentTokens.recordingPreviewIconSize
        // Read by assistive technology; by default the tooltip without its
        // "(shortcut)" hint (#210: play, save, discard had no name and could
        // not be invoked).
        property string accessibleName: tooltipText.replace(/\s*\([^()]*\)$/, "")

        signal clicked()
        activeFocusOnTab: true
        Keys.onSpacePressed: function(event) { if (enabled && !event.isAutoRepeat) clicked() }
        Keys.onReturnPressed: function(event) { if (enabled && !event.isAutoRepeat) clicked() }
        Keys.onEnterPressed: function(event) { if (enabled && !event.isAutoRepeat) clicked() }
        FocusFrame { anchors.fill: parent; showFocus: parent.activeFocus }

        Accessible.role: Accessible.Button
        Accessible.name: accessibleName !== "" ? accessibleName : iconText
        Accessible.description: tooltipText !== Accessible.name ? tooltipText : ""
        Accessible.onPressAction: if (enabled) clicked()

        function updateTooltip() {
            if (!mouseArea.containsMouse || tooltipText === "") {
                root.hideButtonTooltip(this)
                return
            }

            root.showButtonTooltip(tooltipText, this, tooltipPreferredAbove)
        }

        width: Math.max(ComponentTokens.recordingPreviewControlButtonSize,
                        iconSource !== "" ? ComponentTokens.recordingPreviewControlButtonSize : label.implicitWidth + 16)
        height: ComponentTokens.recordingPreviewControlButtonHeight
        radius: SemanticTokens.radiusSmall

        color: {
            if (primary)
                return mouseArea.pressed ? ComponentTokens.recordingPreviewPrimaryButtonPressed
                                         : mouseArea.containsMouse ? ComponentTokens.recordingPreviewPrimaryButtonHover
                                                                   : ComponentTokens.recordingPreviewPrimaryButton
            if (destructive)
                return mouseArea.pressed ? ComponentTokens.recordingPreviewDangerPressed
                                         : mouseArea.containsMouse ? ComponentTokens.recordingPreviewDangerHover
                                                                   : "transparent"
            if (mouseArea.pressed)
                return root.bgPanelPressed
            if (mouseArea.containsMouse)
                return root.bgPanelHover
            if (highlighted)
                return ComponentTokens.editorSelection
            return "transparent"
        }

        Behavior on color {
            ColorAnimation { duration: SemanticTokens.durationInteractionHover }
        }

        Text {
            id: label
            anchors.centerIn: parent
            visible: parent.iconSource === ""
            text: parent.iconText
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.weight: SemanticTokens.fontWeightMedium
            color: parent.primary ? ComponentTokens.recordingPreviewPrimaryButtonIcon
                                  : parent.highlighted ? root.accent : root.textPrimary
        }

        SvgIcon {
            anchors.centerIn: parent
            visible: parent.iconSource !== ""
            source: parent.iconSource
            iconSize: parent.iconSize
            color: parent.primary ? ComponentTokens.recordingPreviewPrimaryButtonIcon
                                  : parent.destructive ? SemanticTokens.statusError
                                                       : parent.highlighted ? root.accent : root.iconColor
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: CursorTokens.clickable

            onEntered: parent.updateTooltip()
            onExited: root.hideButtonTooltip(parent)
            onPressed: root.hideButtonTooltip(parent)
            onCanceled: root.hideButtonTooltip(parent)
            onClicked: {
                root.hideButtonTooltip(parent)
                parent.clicked()
            }
        }

        onTooltipTextChanged: updateTooltip()
    }

    // A caption above the controls it names (#221 §2: hierarchy by grouping).
    component FieldGroup: Column {
        property string title: ""
        width: parent ? parent.width : 0
        spacing: 6
        Text {
            visible: text.length > 0
            width: parent.width
            text: parent.title
            color: root.textSecondary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.weight: SemanticTokens.fontWeightMedium
            wrapMode: Text.WordWrap
        }
    }

    // A labelled on/off setting: the name on the left, the switch on the right.
    component EditorSwitch: Switch {
        id: editorSwitch
        property string tooltipText: ""
        // The backend value the switch shows. A toggle only asks for a change
        // (onClicked); the shown state is re-bound to this value right away, so
        // a rejected or undone change never leaves the switch flipped, while the
        // control stays a checkable switch with a Toggle action for assistive
        // technology (PR #228 review).
        property bool on: false
        checked: on
        onToggled: checked = Qt.binding(function() { return editorSwitch.on })
        // Announced as an on/off control with a Toggle action that goes through
        // the same request path as a click.
        Accessible.role: Accessible.CheckBox
        Accessible.checkable: true
        Accessible.checked: on
        Accessible.onToggleAction: if (enabled) click()
        Accessible.onPressAction: if (enabled) click()
        width: parent ? parent.width : implicitWidth
        padding: 0
        topPadding: 4
        bottomPadding: 4
        opacity: enabled ? 1 : root.disabledControlOpacity
        Accessible.name: text
        Accessible.description: tooltipText
        indicator: Rectangle {
            x: editorSwitch.width - width
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 34
            implicitHeight: 20
            radius: 10
            color: editorSwitch.checked ? root.accent : ComponentTokens.editorControlHover
            border.width: editorSwitch.checked ? 0 : 1
            border.color: root.borderColor
            Rectangle {
                x: editorSwitch.checked ? parent.width - width - 2 : 2
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                radius: 8
                color: "#ffffff"
                Behavior on x { NumberAnimation { duration: SemanticTokens.durationFast } }
            }
        }
        contentItem: Text {
            rightPadding: editorSwitch.indicator.width + 8
            text: editorSwitch.text
            color: root.textPrimary
            font.pixelSize: SemanticTokens.fontSizeSmall
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }
        FocusFrame { anchors.fill: parent; showFocus: editorSwitch.visualFocus }
        ToolTip.visible: hovered && tooltipText.length > 0
        ToolTip.text: tooltipText
        ToolTip.delay: 500
    }

    // One whole-recording category: icon, name, current state, and a chevron.
    // Unavailable categories stay listed, dimmed, with the reason on a second line.
    component CategoryRow: Rectangle {
        id: categoryRow
        property url iconSource: ""
        property string text: ""
        property string summary: ""
        property string unavailableReason: ""
        // False when this category cannot be used (no data, format). The
        // reason shows only then: a whole editor disabled during an export
        // keeps the summaries.
        property bool available: true
        enabled: available
        property string tooltipText: ""
        property bool selected: false
        signal clicked()
        width: parent ? parent.width : 0
        height: Math.max(48, rowColumn.implicitHeight + 16)
        radius: SemanticTokens.radiusSmall
        color: rowMouse.containsMouse && enabled ? ComponentTokens.editorControlHover : "transparent"
        opacity: enabled ? 1 : 0.55
        activeFocusOnTab: enabled
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: available ? summary : unavailableReason
        Accessible.checked: selected
        Accessible.onPressAction: if (enabled) clicked()
        Keys.onReturnPressed: function(event) { if (enabled && !event.isAutoRepeat) clicked() }
        Keys.onSpacePressed: function(event) { if (enabled && !event.isAutoRepeat) clicked() }
        FocusFrame { anchors.fill: parent; showFocus: categoryRow.activeFocus }
        SvgIcon {
            id: rowIcon
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            source: categoryRow.iconSource
            iconSize: 18
            color: root.textPrimary
        }
        Column {
            id: rowColumn
            x: rowIcon.x + rowIcon.width + 12
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - x - 30
            spacing: 2
            Text {
                width: parent.width
                text: categoryRow.text
                color: root.textPrimary
                font.pixelSize: SemanticTokens.fontSizeSmall
                font.weight: SemanticTokens.fontWeightMedium
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: categoryRow.available ? categoryRow.summary : categoryRow.unavailableReason
                color: root.textSecondary
                font.pixelSize: SemanticTokens.fontSizeCaption
                wrapMode: Text.WordWrap
            }
        }
        SvgIcon {
            visible: categoryRow.available
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            source: root.iconUrl("chevron-right")
            iconSize: 16
            color: root.textSecondary
        }
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: categoryRow.enabled
            cursorShape: CursorTokens.clickable
            onClicked: categoryRow.clicked()
        }
        ToolTip.visible: rowMouse.containsMouse && tooltipText.length > 0
        ToolTip.text: tooltipText
        ToolTip.delay: 700
    }

    component SegmentButton: Rectangle {
        id: segmentButton
        property string text: ""
        property string tooltipText: ""
        // Spoken name when the visible text is a symbol ("A+") or empty.
        property string accessibleName: ""
        property bool tooltipPreferredAbove: true
        property bool selected: false
        // One of several mutually exclusive choices (#238): read as a radio
        // button so screen readers announce which one is selected.
        property bool exclusive: false
        property bool primary: false
        property bool destructive: false
        // Text-only: secondary actions that should not compete with buttons.
        property bool quiet: false
        property url iconSource: ""
        property url trailingIconSource: ""
        property real minimumButtonWidth: 64

        signal clicked()
        activeFocusOnTab: true
        Keys.onSpacePressed: function(event) { if (enabled && !event.isAutoRepeat) clicked() }
        Keys.onReturnPressed: function(event) { if (enabled && !event.isAutoRepeat) clicked() }
        Keys.onEnterPressed: function(event) { if (enabled && !event.isAutoRepeat) clicked() }
        FocusFrame { anchors.fill: parent; showFocus: parent.activeFocus }

        function updateTooltip() {
            if (!fmtMouseArea.containsMouse || tooltipText === "") {
                root.hideButtonTooltip(this)
                return
            }

            root.showButtonTooltip(tooltipText, this, tooltipPreferredAbove)
        }

        readonly property color contentColor: primary ? ComponentTokens.recordingPreviewPrimaryButtonIcon
            : destructive ? ComponentTokens.editorDestructiveText
            : root.textPrimary
        implicitWidth: Math.max(minimumButtonWidth, contentRow.implicitWidth + 2 * SemanticTokens.spacing12)
        width: implicitWidth
        height: ComponentTokens.recordingPreviewControlButtonHeight
        radius: SemanticTokens.radiusSmall
        opacity: enabled ? 1.0 : root.disabledControlOpacity

        Accessible.role: exclusive ? Accessible.RadioButton : Accessible.Button
        Accessible.name: accessibleName !== "" ? accessibleName
            : (text !== "" ? text : tooltipText)
        Accessible.description: tooltipText
        Accessible.checkable: exclusive
        Accessible.checked: selected
        Accessible.onPressAction: if (enabled) clicked()

        color: primary ? (fmtMouseArea.pressed ? ComponentTokens.recordingPreviewPrimaryButtonPressed
                          : fmtMouseArea.containsMouse ? ComponentTokens.recordingPreviewPrimaryButtonHover : root.accent)
             : fmtMouseArea.pressed ? root.bgPanelPressed
             : fmtMouseArea.containsMouse ? ComponentTokens.editorControlHover
             : selected ? ComponentTokens.editorSelection
             : quiet ? "transparent" : root.bgPanel

        // Selected: a tint and an accent edge; the label keeps body contrast.
        border.width: primary || (quiet && !selected) ? 0 : 1
        border.color: selected ? root.accent : root.borderColor

        Behavior on color {
            ColorAnimation { duration: SemanticTokens.durationFast }
        }

        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: 6
            SvgIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: segmentButton.iconSource != ""
                source: segmentButton.iconSource
                iconSize: 15
                color: segmentButton.contentColor
            }
            Text {
                id: fmtLabel
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
                text: segmentButton.text
                font.pixelSize: SemanticTokens.fontSizeSmall
                font.family: SemanticTokens.fontFamily
                font.weight: segmentButton.primary ? SemanticTokens.fontWeightSemiBold : SemanticTokens.fontWeightMedium
                color: segmentButton.contentColor
            }
            SvgIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: segmentButton.trailingIconSource != ""
                source: segmentButton.trailingIconSource
                iconSize: 13
                color: segmentButton.primary ? segmentButton.contentColor : root.textSecondary
            }
        }

        MouseArea {
            id: fmtMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: CursorTokens.clickable

            onEntered: parent.updateTooltip()
            onExited: root.hideButtonTooltip(parent)
            onPressed: root.hideButtonTooltip(parent)
            onCanceled: root.hideButtonTooltip(parent)
            onClicked: {
                root.hideButtonTooltip(parent)
                parent.clicked()
            }
        }

        onTooltipTextChanged: updateTooltip()
    }
}
