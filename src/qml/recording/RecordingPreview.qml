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
    readonly property string inspectorTitle: cropOverlay.editing ? qsTr("Crop")
        : backend.selectedMask >= 0 ? qsTr("Mask")
        : backend.selectedZoom >= 0 ? qsTr("Zoom segment")
        : backend.selectedOverlay >= 0 ? qsTr("Overlay")
        : backend.selectedPointerHide >= 0 ? qsTr("Hidden pointer")
        : selectedPart >= 0 ? qsTr("Video clip")
        : appearanceOpen ? qsTr("Canvas · whole recording")
        : pointerOptionsOpen ? qsTr("Pointer · whole recording")
        : audioOpen ? qsTr("Audio · whole recording")
        : keysOpen ? qsTr("Keys · whole recording")
        : qsTr("Project settings")

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
    function openSection(section) {
        if (!commitInspector()) return
        if (cropOverlay.editing && !applyCropWithPendingInput()) return
        clearSelection()
        pointerOptionsOpen = section === "pointer"
        appearanceOpen = section === "appearance"
        audioOpen = section === "audio"
        keysOpen = section === "keys"
        inspectorOpen = true
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
    readonly property var pointerSmoothingLabels: [qsTr("Smooth: Off"), qsTr("Smooth: Low"),
                                                   qsTr("Smooth: Medium"), qsTr("Smooth: High")]
    // #186: choices in ms (0 = off) and blur levels in backend order.
    readonly property var pointerIdleChoices: [0, 1000, 2000, 5000]
    readonly property var pointerStillEndChoices: [0, 500, 1000, 2000]
    readonly property var pointerBlurLabels: [qsTr("Motion Blur: Off"), qsTr("Motion Blur: Low"),
                                              qsTr("Motion Blur: Medium"), qsTr("Motion Blur: High")]
    // Project settings category currently shown in the inspector.
    property bool pointerOptionsOpen: false
    // Text and image overlays (#188), in OverlaySegment order.
    readonly property var selectedOverlayData: backend.selectedOverlay >= 0 && backend.selectedOverlay < backend.overlays.length
        ? backend.overlays[backend.selectedOverlay] : null
    readonly property var overlayColors: ["#ffffff", "#000000", "#ffd60a", "#ff453a", "#0a84ff"]
    readonly property var overlayBackgroundLabels: [qsTr("No Background"), qsTr("Dark Background"), qsTr("Light Background")]
    readonly property var overlayAlignLabels: [qsTr("Align Left"), qsTr("Align Center"), qsTr("Align Right")]
    // Separate audio tracks (#189).
    property bool audioOpen: false
    readonly property var audioGainChoices: [-12, -6, 0, 6]
    function gainLabel(db) {
        return (db > 0 ? "+" : "") + Number(db).toLocaleString(Qt.locale(), "f", 0) + " dB"
    }
    // Shortcut hints (#187), in KeyOverlaySettings order.
    property bool keysOpen: false
    readonly property var keyPositionLabels: [qsTr("Bottom"), qsTr("Bottom Left"), qsTr("Bottom Right"), qsTr("Top")]
    readonly property var keySizeLabels: [qsTr("Small"), qsTr("Medium"), qsTr("Large")]
    readonly property var keyDisplayChoices: [1000, 1500, 2500]
    // The frame around the export (#184), in RecordingAppearance order.
    property bool appearanceOpen: false
    readonly property var appearanceAmountLabels: [qsTr("None"), qsTr("Small"), qsTr("Medium"), qsTr("Large")]
    readonly property var appearanceRatioLabels: [qsTr("Ratio: Auto"), "1:1", "16:9", "4:3", "9:16", "2:1"]
    // Shown in the preview: the frame, unless the crop is being edited or the
    // original is compared.
    readonly property bool appearanceShown: backend.appearanceActive && !cropOverlay.editing && !root.comparingOriginal
    readonly property rect appearanceCanvas: {
        void backend.appearanceEnabled; void backend.appearancePadding; void backend.appearanceAspectRatio
        void backend.cropRect; void backend.videoSize; void backend.selectedFormat
        return backend.appearanceCanvasInView(root.zoomTarget)
    }
    // MP4 export options (#190), in Mp4ExportOptions order.
    readonly property var mp4ResolutionLabels: [qsTr("Size: Original"), qsTr("Size: 1080p"), qsTr("Size: 720p"),
                                                qsTr("Size: Custom Width")]
    readonly property var mp4FrameRateChoices: [0, 60, 30, 15]
    readonly property var mp4QualityLabels: [qsTr("Quality: Default"), qsTr("Quality: Low"),
                                             qsTr("Quality: Medium"), qsTr("Quality: High")]
    readonly property var selectedPointerHideData: backend.selectedPointerHide >= 0
        ? backend.pointerHiddenRanges[backend.selectedPointerHide] : null
    function secondsLabel(ms) {
        return Number(ms / 1000).toLocaleString(Qt.locale(), "f", ms % 1000 ? 1 : 0)
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

    // Width a centred row of controls needs on one line: its visible items
    // side by side. Wrapping rows take this when it fits, else wrap (#176 review).
    function rowContentWidth(row) {
        let width = 0
        let count = 0
        for (let i = 0; i < row.children.length; ++i) {
            const child = row.children[i]
            if (child.visible && child.width > 0) {
                width += child.width
                ++count
            }
        }
        return width + Math.max(0, count - 1) * row.spacing
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
    function speedLabel(speed) {
        return Number(speed).toLocaleString(Qt.locale(), "f", speed % 1 ? 1 : 0) + "×"
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
        const end = endField.edited ? backend.parseTime(endField.text) : endField.timeMs
        // Unreadable text marks its own field; a rejected range marks what was typed.
        const unreadable = start < 0 || end < 0
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
        anchors.fill: parent
        enabled: !backend.isProcessing && !backend.projectLoading
        inspectorVisible: root.wideEditor || root.inspectorOpen
        inspectorOverlay: !root.wideEditor
        inspectorTitle: root.inspectorTitle
        onProjectRequested: root.openSection("")
        onInspectorCloseRequested: {
            if (root.commitInspector()) root.inspectorOpen = false
        }
    }

    RowLayout {
        parent: editorShell.headerContent
        anchors.fill: parent
        spacing: SemanticTokens.spacing8
        Text {
            text: backend.isProject ? qsTr("Recording project") : qsTr("Recording")
            color: root.textPrimary
            font.pixelSize: SemanticTokens.fontSizeBody
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
        SegmentButton {
            text: qsTr("Settings")
            onClicked: root.inspectorOpen = true
        }
        SegmentButton {
            objectName: "previewExportOptions"
            text: qsTr("Export…")
            primary: true
            onClicked: root.openExport()
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
                // While a mask is selected its editor owns the video.
                enabled: !cropOverlay.editing && !(backend.selectedMask >= 0)
                cursorShape: backend.selectedZoom >= 0 ? CursorTokens.pickPoint : CursorTokens.clickable
                onClicked: function(mouse) {
                    // With a zoom selected, a click moves its centre there.
                    if (backend.selectedZoom >= 0) {
                        const p = root.unzoomPoint(mouse.x, mouse.y)
                        backend.setZoomFocusFromView(backend.selectedZoom, p, videoPlayer.contentRect)
                        return
                    }
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

        Flow {
            id: timelineCommands
            Layout.fillWidth: true
            Layout.preferredHeight: Math.ceil(implicitHeight)
            Layout.leftMargin: 12
            spacing: 6
                SegmentButton {
                    objectName: "previewSplit"
                    enabled: backend.timelineEditsAvailable && videoPlayer.duration > 0
                    minimumButtonWidth: 0
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
                    enabled: backend.timelineEditsAvailable && root.playheadPartData !== null
                             && backend.timelineParts.length > 1
                    minimumButtonWidth: 0
                    text: root.playheadPartData && root.playheadPartData.deleted ? qsTr("Keep Part") : qsTr("Cut Part")
                    tooltipText: root.playheadPartData && root.playheadPartData.deleted
                        ? qsTr("Put the part at the playhead back (Delete)")
                        : qsTr("Leave the part at the playhead out of the export (Delete)")
                    selected: root.playheadPartData !== null && root.playheadPartData.deleted
                    onClicked: backend.setPartDeleted(root.playheadPart, !root.playheadPartData.deleted)
                }
                EditorChoice {
                    objectName: "previewPartSpeed"
                    width: 150
                    enabled: backend.timelineEditsAvailable && root.playheadPartData !== null
                             && !root.playheadPartData.deleted
                    tooltipText: qsTr("How fast the part at the playhead plays in the export; changed parts are silent")
                    label: qsTr("Export clip speed")
                    choices: root.partSpeeds.map(v => root.speedLabel(v))
                    currentIndex: root.partSpeeds.indexOf(root.playheadPartData ? root.playheadPartData.speed : 1)
                    onActivated: function(index) { backend.setPartSpeed(root.playheadPart, root.partSpeeds[index]) }
                }
            SegmentButton {
                objectName: "previewAddEffect"
                text: qsTr("Add effect")
                onClicked: if (root.commitInspector()) effectMenu.open()
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
                        if (!root.commitInspector()) return
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
                        if (!root.commitInspector()) return
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
                        if (!root.commitInspector()) return
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
                        if (!root.commitInspector()) return
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
                        if (!root.commitInspector()) return
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
        Flickable {
            id: timelineArea
            objectName: "previewTimeline"
            clip: true
            contentWidth: width
            contentHeight: 84 + 30 * (1 + backend.masks.length + backend.overlays.length + backend.pointerHiddenRanges.length)
            boundsBehavior: Flickable.StopAtBounds
            Layout.fillWidth: true
            Layout.preferredHeight: root.height < 600 ? 140 : 190
            Layout.leftMargin: SemanticTokens.spacing16
            Layout.rightMargin: SemanticTokens.spacing16

            readonly property real trackY: 22
            readonly property real trackHeight: 14
            readonly property real handleWidth: 8
            readonly property real playheadRadius: 7
            readonly property real dur: videoPlayer.duration > 0 ? videoPlayer.duration : 1

            function posToX(ms) {
                return (ms / dur) * trackRect.width + trackRect.x
            }

            function xToPos(x) {
                var fraction = Math.max(0, Math.min(1, (x - trackRect.x) / trackRect.width))
                return Math.round(fraction * dur)
            }

            MouseArea {
                y: 52
                width: parent.width
                height: Math.max(0, timelineArea.contentHeight - y)
                onClicked: root.openSection("")
            }

            Text {
                y: timelineArea.trackY
                text: qsTr("Video")
                color: root.textSecondary
                font.pixelSize: SemanticTokens.fontSizeCaption
            }
            Rectangle {
                id: trackRect
                x: 76
                y: timelineArea.trackY
                width: timelineArea.width - 76 - timelineArea.handleWidth
                height: timelineArea.trackHeight
                radius: 3
                color: root.bgPanel
                border.width: 1
                border.color: root.borderColor
            }

            Rectangle {
                x: trackRect.x
                y: trackRect.y
                width: Math.max(0, timelineArea.posToX(videoPlayer.position) - trackRect.x)
                height: trackRect.height
                radius: 3
                color: root.accent
            }

            Rectangle {
                visible: backend.hasTrim
                x: trackRect.x
                y: trackRect.y - 4
                width: Math.max(0, timelineArea.posToX(backend.trimStart) - trackRect.x)
                height: trackRect.height + 8
                color: root.trimOverlayColor
                radius: 2
            }

            Rectangle {
                visible: backend.hasTrim
                x: timelineArea.posToX(backend.trimEnd)
                y: trackRect.y - 4
                width: Math.max(0, trackRect.x + trackRect.width - timelineArea.posToX(backend.trimEnd))
                height: trackRect.height + 8
                color: root.trimOverlayColor
                radius: 2
            }

            Rectangle {
                id: trimStartHandle
                visible: backend.hasTrim
                x: timelineArea.posToX(backend.trimStart) - width / 2
                y: trackRect.y - 6
                width: timelineArea.handleWidth
                height: trackRect.height + 12
                radius: 2
                color: root.isDraggingTrimStart ? root.accentHover : root.accent
            }

            Rectangle {
                id: trimEndHandle
                visible: backend.hasTrim
                x: timelineArea.posToX(backend.trimEnd) - width / 2
                y: trackRect.y - 6
                width: timelineArea.handleWidth
                height: trackRect.height + 12
                radius: 2
                color: root.isDraggingTrimEnd ? root.accentHover : root.accent
            }

            // Cut-out parts are shaded over the track, parts at another speed
            // are labelled (#185).
            Repeater {
                model: backend.timelineParts
                Item {
                    required property int index
                    required property var modelData
                    objectName: "previewTimelinePart" + index
                    x: timelineArea.posToX(modelData.start)
                    y: trackRect.y - 4
                    width: Math.max(1, timelineArea.posToX(modelData.end) - x)
                    height: trackRect.height + 8
                    z: 1
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: 2
                        border.color: root.accent
                        visible: root.selectedPart === parent.index
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: parent.modelData.deleted
                        color: root.trimOverlayColor
                        border.width: 1
                        border.color: root.textSecondary
                        radius: 2
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.top
                        visible: !parent.modelData.deleted && parent.modelData.speed !== 1 && parent.width > 16
                        text: root.speedLabel(parent.modelData.speed)
                        color: root.accent
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.weight: SemanticTokens.fontWeightMedium
                    }
                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData.deleted
                        ? qsTr("Cut from %1 to %2").arg(backend.formatPreciseTime(modelData.start))
                                                   .arg(backend.formatPreciseTime(modelData.end))
                        : qsTr("Part from %1 to %2 at %3").arg(backend.formatPreciseTime(modelData.start))
                                                          .arg(backend.formatPreciseTime(modelData.end))
                                                          .arg(root.speedLabel(modelData.speed))
                }
            }

            // Text and image overlays (#188), at the bottom of the timeline.
            Repeater {
                model: backend.overlays
                Item {
                    required property int index
                    required property var modelData
                    y: 84 + 30 * (backend.masks.length + index)
                    width: timelineArea.width
                    height: 28
                    z: 2
                    Text {
                        width: 70
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.kind === 0 ? qsTr("Text") : qsTr("Image")
                        color: root.textSecondary
                        elide: Text.ElideRight
                        font.pixelSize: SemanticTokens.fontSizeCaption
                    }
                    EditorTimelineSegment {
                        objectName: "previewOverlaySegment" + index
                        startMs: modelData.start
                        endMs: modelData.end < 0 ? timelineArea.dur : modelData.end
                        lowerMs: 0
                        upperMs: timelineArea.dur
                        minimumDuration: 100
                        pixelsPerMs: trackRect.width / timelineArea.dur
                        trackX: trackRect.x
                        selected: backend.selectedOverlay === index
                        label: (modelData.kind === 0 ? qsTr("Text") : qsTr("Image")) + " · " + backend.formatPreciseTime(startMs) + " – " + backend.formatPreciseTime(endMs)
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
            }

            // Split points; drag to move one, double-click to join its parts.
            Repeater {
                model: backend.timelineSplits
                Rectangle {
                    required property int index
                    required property var modelData
                    objectName: "previewTimelineSplit" + index
                    // While dragged it moves here only: changing the model on
                    // every move would rebuild this delegate and end the drag
                    // (PR #199 review). The edit is made once, on release.
                    property real dragMs: -1
                    x: timelineArea.posToX(dragMs >= 0 ? dragMs : modelData) - width / 2
                    y: trackRect.y - 7
                    width: 3
                    height: trackRect.height + 14
                    radius: 1
                    z: 3
                    color: root.textPrimary
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Split at %1").arg(backend.formatPreciseTime(modelData))
                    Accessible.onPressAction: backend.removeSplit(index)
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: CursorTokens.resizeHorizontal
                        onPressed: { parent.dragMs = -1; videoPlayer.pause() }
                        onPositionChanged: function(mouse) {
                            const ms = timelineArea.xToPos(mapToItem(timelineArea, mouse.x, mouse.y).x)
                            const legal = backend.legalSplitPosition(parent.index, ms)
                            if (legal >= 0) parent.dragMs = legal
                        }
                        function finishDrag() {
                            const target = parent.dragMs
                            parent.dragMs = -1
                            if (target >= 0 && backend.moveSplit(parent.index, target))
                                backend.commitTimelineEdit()
                        }
                        onReleased: finishDrag()
                        onCanceled: parent.dragMs = -1
                        onDoubleClicked: backend.removeSplit(parent.index)
                    }
                }
            }

            // Masks just above the track; click one to edit it.
            Repeater {
                model: backend.masks
                Item {
                    required property int index
                    required property var modelData
                    y: 84 + 30 * index
                    width: timelineArea.width
                    height: 28
                    z: 2
                    Text {
                        width: 70
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Mask")
                        color: root.textSecondary
                        elide: Text.ElideRight
                        font.pixelSize: SemanticTokens.fontSizeCaption
                    }
                    EditorTimelineSegment {
                        objectName: "previewMaskSegment" + index
                        startMs: modelData.start
                        endMs: modelData.end < 0 ? timelineArea.dur : modelData.end
                        lowerMs: 0
                        upperMs: timelineArea.dur
                        minimumDuration: 1
                        pixelsPerMs: trackRect.width / timelineArea.dur
                        trackX: trackRect.x
                        selected: backend.selectedMask === index
                        label: (qsTr("Mask")) + " · " + backend.formatPreciseTime(startMs) + " – " + backend.formatPreciseTime(endMs)
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
            }

            // Ranges without a pointer (#186), just below the track.
            Repeater {
                model: backend.pointerHiddenRanges
                Item {
                    required property int index
                    required property var modelData
                    y: 84 + 30 * (backend.masks.length + backend.overlays.length + index)
                    width: timelineArea.width
                    height: 28
                    z: 2
                    Text {
                        width: 70
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Pointer")
                        color: root.textSecondary
                        elide: Text.ElideRight
                        font.pixelSize: SemanticTokens.fontSizeCaption
                    }
                    EditorTimelineSegment {
                        objectName: "previewPointerHideSegment" + index
                        startMs: modelData.start
                        endMs: modelData.end < 0 ? timelineArea.dur : modelData.end
                        lowerMs: 0
                        upperMs: timelineArea.dur
                        minimumDuration: 100
                        pixelsPerMs: trackRect.width / timelineArea.dur
                        trackX: trackRect.x
                        selected: backend.selectedPointerHide === index
                        label: (qsTr("Pointer")) + " · " + backend.formatPreciseTime(startMs) + " – " + backend.formatPreciseTime(endMs)
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
            }

            // Zoom segments above the track; click one to edit it.
            Repeater {
                model: backend.zoomSegments
                Item {
                    required property int index
                    required property var modelData
                    y: 54
                    width: timelineArea.width
                    height: 28
                    z: 2
                    Text {
                        width: 70
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Zoom")
                        color: root.textSecondary
                        elide: Text.ElideRight
                        font.pixelSize: SemanticTokens.fontSizeCaption
                    }
                    EditorTimelineSegment {
                        objectName: "previewZoomSegment" + index
                        startMs: modelData.start
                        endMs: modelData.end < 0 ? timelineArea.dur : modelData.end
                        lowerMs: index > 0 ? backend.zoomSegments[index - 1].end : 0
                        upperMs: index + 1 < backend.zoomSegments.length ? backend.zoomSegments[index + 1].start : timelineArea.dur
                        minimumDuration: 500
                        pixelsPerMs: trackRect.width / timelineArea.dur
                        trackX: trackRect.x
                        selected: backend.selectedZoom === index
                        label: (qsTr("Zoom")) + " · " + backend.formatPreciseTime(startMs) + " – " + backend.formatPreciseTime(endMs)
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
            }

            Rectangle {
                id: playhead
                x: timelineArea.posToX(videoPlayer.position) - width / 2
                y: trackRect.y - (height - trackRect.height) / 2
                width: timelineArea.playheadRadius * 2
                height: timelineArea.playheadRadius * 2
                radius: timelineArea.playheadRadius
                color: ComponentTokens.recordingPreviewPlayhead
                border.width: 1
                border.color: ComponentTokens.recordingPreviewPlayheadBorder
            }

            MouseArea {
                width: parent.width
                height: 52
                hoverEnabled: true

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
                    if (!root.commitInspector()) { mouse.accepted = false; return }
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
                    else
                        root.selectPart(backend.timelinePartAt(videoPlayer.position))
                }

                onCanceled: {
                    const trimmed = draggingTrimStart || draggingTrimEnd
                    resetInteractionState()
                    if (trimmed)
                        backend.commitTrimEdit()
                }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: timelineArea.handleWidth
                y: 0
                text: backend.formatTime(videoPlayer.position)
                font.pixelSize: SemanticTokens.fontSizeSmall
                font.family: SemanticTokens.fontFamily
                color: root.textSecondary
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: timelineArea.handleWidth
                y: 0
                visible: !backend.hasTrim
                text: backend.formatTime(videoPlayer.duration)
                font.pixelSize: SemanticTokens.fontSizeSmall
                font.family: SemanticTokens.fontFamily
                color: root.textSecondary
            }

            // What Save will produce: encoded pixel sizes (not window sizes), kept time, audio.
            // Centred under the track between the position and duration labels.
            Text {
                objectName: "previewOutputSummary"
                anchors.horizontalCenter: parent.horizontalCenter
                y: 0
                width: Math.max(0, parent.width - 2 * 150)
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                color: root.textSecondary
                font.pixelSize: SemanticTokens.fontSizeSmall
                font.family: SemanticTokens.fontFamily
                text: {
                    if (backend.videoSize.width <= 0)
                        return ""
                    const source = backend.videoSize.width + " × " + backend.videoSize.height
                    const out = backend.hasCrop ? backend.cropRect.width + " × " + backend.cropRect.height : source
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
            }

            // Exact trim entry ("m:ss.mmm") while a trim range exists.
            Row {
                objectName: "previewTrimFields"
                visible: backend.hasTrim
                anchors.right: parent.right
                anchors.rightMargin: timelineArea.handleWidth
                y: timelineArea.contentHeight - height
                spacing: SemanticTokens.spacing4

                TimeField {
                    id: trimStartField
                    objectName: "previewTrimStartField"
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
                    accessibleName: qsTr("Trim end")
                    timeMs: backend.trimEnd
                    onCommitted: function(ms) { backend.setTrimFromInput(backend.trimStart, ms) }
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
                x: 12
                y: 8
                width: parent.width - 24
                spacing: SemanticTokens.spacing8
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
                    text: qsTr("Settings apply to the whole recording. Select an effect on the timeline to edit a time range.")
                    wrapMode: Text.WordWrap
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                }

                SegmentButton {
                    objectName: "previewPointerOptions"

                    enabled: backend.hasPointerData && backend.cursorEffectsAvailable
                    minimumButtonWidth: 0
                    text: qsTr("Pointer")
                    tooltipText: qsTr("Hide when idle, motion blur, a still ending and time ranges without a pointer")
                    selected: backend.cursorIdleHide > 0 || backend.cursorMotionBlur > 0
                              || backend.cursorStillEnd > 0 || backend.pointerHiddenRanges.length > 0
                    onClicked: root.openSection("pointer")
                }

                // Text and image overlays (#188).


                // Cuts and speed changes (#185), at the playhead.

                Text {
                    width: parent.width
                    visible: !backend.hasPointerData || !backend.cursorEffectsAvailable
                    text: !backend.hasPointerData ? qsTr("No pointer metadata was recorded.") : qsTr("Pointer effects are unavailable in this export format on this system.")
                    wrapMode: Text.WordWrap
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                }
                SegmentButton {
                    objectName: "previewAudioOptions"

                    enabled: backend.hasSeparateTracks && backend.audioMixAvailable
                    minimumButtonWidth: 0
                    text: qsTr("Audio")
                    tooltipText: backend.audioMixAvailable
                        ? qsTr("Microphone and system audio volume, mute, and noise reduction")
                        : qsTr("Mixing the two tracks is not supported for MP4 on this system yet")
                    selected: backend.audioMixAvailable && (backend.microphoneGainDb !== 0 || backend.systemAudioGainDb !== 0
                              || backend.microphoneMuted || backend.systemAudioMuted || backend.microphoneNoiseReduction)
                    onClicked: root.openSection("audio")
                }
                Text {
                    width: parent.width
                    visible: !backend.hasSeparateTracks || !backend.audioMixAvailable
                    text: !backend.hasSeparateTracks ? qsTr("This recording has no separate microphone and system tracks. Existing mixed audio is preserved.") : qsTr("Separate-track mixing is unavailable in this export format on this system.")
                    wrapMode: Text.WordWrap
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                }
                SegmentButton {
                    objectName: "previewKeyOptions"

                    enabled: backend.hasKeyData && backend.keyOverlayAvailable
                    minimumButtonWidth: 0
                    text: qsTr("Keys")
                    tooltipText: backend.keyOverlayAvailable
                        ? qsTr("Show the shortcuts pressed while recording (%1 recorded)").arg(backend.recordedKeyCount)
                        : qsTr("Shortcut hints need GIF or WebP on this system")
                    selected: backend.keyOverlayAvailable && backend.keyOverlayEnabled
                    onClicked: root.openSection("keys")
                }
                Text {
                    width: parent.width
                    visible: !backend.hasKeyData || !backend.keyOverlayAvailable
                    text: !backend.hasKeyData ? qsTr("No shortcut metadata was recorded.") : qsTr("Shortcut hints are unavailable in this export format on this system.")
                    wrapMode: Text.WordWrap
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                }
                SegmentButton {
                    objectName: "previewAppearanceOptions"
                    enabled: backend.appearanceAvailable
                    minimumButtonWidth: 0
                    text: qsTr("Frame")
                    tooltipText: backend.appearanceAvailable
                        ? qsTr("A background, padding, rounded corners, a shadow and a border around the video")
                        : qsTr("A frame needs GIF or WebP on this system")
                    selected: backend.appearanceActive
                    onClicked: root.openSection("appearance")
                }
            }

            Column {
                id: clipPanel
                objectName: "previewClipPanel"
                x: 12; y: 8
                width: parent.width - 24
                spacing: 12
                visible: root.selectedPartData !== null && !cropOverlay.editing
                Text {
                    width: parent.width
                    text: root.selectedPartData ? qsTr("Source range: %1 – %2")
                        .arg(backend.formatPreciseTime(root.selectedPartData.start))
                        .arg(backend.formatPreciseTime(root.selectedPartData.end)) : ""
                    wrapMode: Text.WordWrap
                    color: root.textSecondary
                }
                EditorChoice {
                    label: qsTr("Export clip speed")
                    tooltipText: qsTr("Changes the selected clip in the exported file. Changed-speed clips are silent.")
                    choices: root.partSpeeds.map(v => root.speedLabel(v))
                    currentIndex: root.partSpeeds.indexOf(root.selectedPartData ? root.selectedPartData.speed : 1)
                    enabled: backend.timelineEditsAvailable && root.selectedPartData !== null && !root.selectedPartData.deleted
                    onActivated: function(index) {
                        const part = root.selectedPart
                        backend.setPartSpeed(part, root.partSpeeds[index])
                        root.selectedPart = part
                    }
                }
                SegmentButton {
                    text: root.selectedPartData && root.selectedPartData.deleted ? qsTr("Keep clip") : qsTr("Cut clip")
                    enabled: backend.timelineEditsAvailable && backend.timelineParts.length > 1
                    onClicked: {
                        const part = root.selectedPart
                        backend.setPartDeleted(part, !root.selectedPartData.deleted)
                        root.selectedPart = part
                    }
                }
            }

            // Editing one mask, in the contextual inspector.
            Column {
                id: maskPanel
                objectName: "previewMaskPanel"
                x: 12
                y: 8
                width: parent.width - 24
                spacing: SemanticTokens.spacing12
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

                Flow {
                    width: parent.width
                    spacing: 6
                Repeater {
                    model: root.maskStyleLabels
                    SegmentButton {
                        required property int index
                        required property string modelData
                        objectName: "previewMaskStyle" + index
                        minimumButtonWidth: 0
                        text: modelData
                        tooltipText: index === 0 ? qsTr("Cover with solid black")
                                                 : qsTr("Weaken detail; it may still be possible to recognise or recover")
                        selected: root.selectedMaskData !== null && root.selectedMaskData.style === index
                        onClicked: backend.setMaskStyle(backend.selectedMask, index)
                    }
                }
                }
                SegmentButton {
                    objectName: "previewMaskWhole"
                    minimumButtonWidth: 0
                    text: qsTr("Whole Recording")
                    tooltipText: qsTr("Mask for the whole recording, or only for a time range")
                    selected: maskPanel.wholeRecording
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
                Column {
                    width: parent.width
                    spacing: 4
                    visible: !maskPanel.wholeRecording
                    Text { text: qsTr("Mask start"); color: root.textSecondary; font.pixelSize: SemanticTokens.fontSizeCaption }
                TimeField {
                    width: parent.width
                    id: maskStartField
                    objectName: "previewMaskStartField"
                    accessibleName: qsTr("Mask start")
                    timeMs: root.selectedMaskData ? root.selectedMaskData.start : 0
                    onCommitted: function(ms) {
                        invalid = !backend.setMaskRange(backend.selectedMask, ms, root.selectedMaskData.end)
                    }
                }
                }
                Column {
                    width: parent.width
                    spacing: 4
                    visible: !maskPanel.wholeRecording
                    Text { text: qsTr("Mask end"); color: root.textSecondary; font.pixelSize: SemanticTokens.fontSizeCaption }
                TimeField {
                    width: parent.width
                    id: maskEndField
                    objectName: "previewMaskEndField"
                    accessibleName: qsTr("Mask end")
                    timeMs: root.selectedMaskData && root.selectedMaskData.end >= 0 ? root.selectedMaskData.end : timelineArea.dur
                    onCommitted: function(ms) {
                        invalid = !backend.setMaskRange(backend.selectedMask, root.selectedMaskData.start, ms)
                    }
                }
                }
                SegmentButton {
                    objectName: "previewMaskDelete"
                    minimumButtonWidth: 0
                    text: qsTr("Delete")
                    tooltipText: qsTr("Delete this mask")
                    onClicked: backend.removeMask(backend.selectedMask)
                }
                SegmentButton {
                    objectName: "previewMaskDone"
                    minimumButtonWidth: 0
                    text: qsTr("Back to project")
                    tooltipText: qsTr("Drag the mask to move it, or its corner to resize it. Done finishes editing.")
                    // Typed times are applied first; invalid ones keep the editor
                    // open, so Save cannot skip them (#177 review).
                    onClicked: {
                        if (maskPanel.wholeRecording || root.commitPendingRange(maskStartField, maskEndField,
                                (start, end) => backend.setMaskRange(backend.selectedMask, start, end)))
                            backend.selectedMask = -1
                    }
                }
            }

            // Project-wide pointer effects.
            Column {
                id: pointerPanel
                objectName: "previewPointerPanel"
                x: 12
                y: 8
                width: parent.width - 24
                spacing: SemanticTokens.spacing12
                visible: root.selectedPart < 0 && root.pointerOptionsOpen && backend.cursorEffectsAvailable
                         && !(backend.selectedPointerHide >= 0) && !(backend.selectedMask >= 0) && !(backend.selectedOverlay >= 0)
                         && !(backend.selectedZoom >= 0) && !cropOverlay.editing

                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewPointerToggle"
                    visible: backend.hasPointerData
                    enabled: backend.cursorEffectsAvailable
                    text: qsTr("Pointer")
                    property string tooltipText: backend.showCursor ? qsTr("Hide the Mouse Pointer") : qsTr("Show the Mouse Pointer")
                    checked: backend.showCursor && backend.cursorEffectsAvailable
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
                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewClickRipple"
                    visible: backend.hasPointerData
                    enabled: backend.clickEffectsAvailable
                    text: qsTr("Clicks")
                    property string tooltipText: backend.clickRipple ? qsTr("Hide Click Highlights") : qsTr("Highlight Clicks")
                    checked: backend.clickRipple && backend.clickEffectsAvailable
                    onClicked: backend.clickRipple = !backend.clickRipple
                }
                EditorChoice {
                    objectName: "previewPointerIdle"
                    tooltipText: qsTr("Fade the pointer out when it stops moving; it comes back before it moves or clicks")
                    label: qsTr("Hide when idle")
                    choices: root.pointerIdleChoices.map(v => v === 0 ? qsTr("Off") : root.secondsLabel(v) + qsTr(" s"))
                    currentIndex: root.pointerIdleChoices.indexOf(backend.cursorIdleHide)
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
                    choices: root.pointerStillEndChoices.map(v => v === 0 ? qsTr("Off") : root.secondsLabel(v) + qsTr(" s"))
                    currentIndex: root.pointerStillEndChoices.indexOf(backend.cursorStillEnd)
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
                SegmentButton {
                    objectName: "previewPointerOptionsDone"
                    minimumButtonWidth: 0
                    text: qsTr("Back to project")
                    tooltipText: qsTr("Back to the export options")
                    onClicked: root.pointerOptionsOpen = false
                }
            }

            // Editing one text or image overlay (#188), in the contextual inspector.
            Column {
                id: overlayPanel
                objectName: "previewOverlayPanel"
                x: 12
                y: 8
                width: parent.width - 24
                spacing: SemanticTokens.spacing12
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

                ScrollView {
                    visible: overlayPanel.isText
                    width: 180
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
                            root.forceActiveFocus()
                            event.accepted = true
                        }
                        background: Rectangle {
                            radius: SemanticTokens.radiusSmall
                            color: root.bgPanel
                            border.width: 1
                            border.color: overlayTextField.activeFocus ? root.accent : root.borderColor
                        }
                    }
                }
                SettingsCombo {
                    objectName: "previewOverlayFont"
                    visible: overlayPanel.isText
                    width: 220
                    labelWidth: 60
                    controlWidth: 160
                    minimumPopupWidth: 260
                    label: qsTr("Font")
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
                SegmentButton {
                    objectName: "previewOverlaySmaller"
                    minimumButtonWidth: 0
                    text: "A\u2212"
                    tooltipText: qsTr("Smaller")
                    onClicked: backend.setOverlaySize(backend.selectedOverlay, root.selectedOverlayData.size / 1.25)
                }
                SegmentButton {
                    objectName: "previewOverlayLarger"
                    minimumButtonWidth: 0
                    text: "A+"
                    tooltipText: qsTr("Larger")
                    onClicked: backend.setOverlaySize(backend.selectedOverlay, root.selectedOverlayData.size * 1.25)
                }
                Flow {
                    width: parent.width
                    spacing: 6
                Repeater {
                    model: overlayPanel.isText ? root.overlayColors : []
                    Rectangle {
                        required property int index
                        required property string modelData
                        objectName: "previewOverlayColor" + index
                        readonly property bool chosen: root.selectedOverlayData !== null
                            && Qt.colorEqual(root.selectedOverlayData.color, modelData)
                        width: ComponentTokens.recordingPreviewControlButtonHeight
                        height: width
                        radius: width / 2
                        color: modelData
                        border.width: chosen ? 2 : 1
                        border.color: chosen ? root.accent : root.borderColor
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Text colour %1").arg(index + 1)
                        Accessible.onPressAction: backend.setOverlayColor(backend.selectedOverlay, modelData)
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: CursorTokens.clickable
                            onClicked: backend.setOverlayColor(backend.selectedOverlay, parent.modelData)
                        }
                    }
                }
                }
                EditorChoice {
                    objectName: "previewOverlayBackground"
                    visible: overlayPanel.isText
                    tooltipText: qsTr("A box behind the text, so it reads on any picture")
                    label: qsTr("Text background")
                    choices: root.overlayBackgroundLabels
                    currentIndex: root.selectedOverlayData ? root.selectedOverlayData.background : 0
                    onActivated: function(index) { backend.setOverlayBackground(backend.selectedOverlay, index) }
                }
                EditorChoice {
                    objectName: "previewOverlayAlign"
                    visible: overlayPanel.isText
                    tooltipText: qsTr("How lines line up")
                    label: qsTr("Text alignment")
                    choices: root.overlayAlignLabels
                    currentIndex: root.selectedOverlayData ? root.selectedOverlayData.align : 0
                    onActivated: function(index) { backend.setOverlayAlign(backend.selectedOverlay, index) }
                }
                EditorChoice {
                    objectName: "previewOverlayOpacity"
                    visible: !overlayPanel.isText
                    tooltipText: qsTr("How see-through the image is")
                    label: qsTr("Image opacity")
                    choices: ["100%", "75%", "50%"]
                    currentIndex: [1, 0.75, 0.5].indexOf(root.selectedOverlayData ? root.selectedOverlayData.opacity : 1)
                    onActivated: function(index) { backend.setOverlayOpacity(backend.selectedOverlay, [1, 0.75, 0.5][index]) }
                }
                SegmentButton {
                    objectName: "previewOverlayWhole"
                    minimumButtonWidth: 0
                    text: qsTr("Whole Recording")
                    tooltipText: qsTr("Show it for the whole recording, or only for a time range")
                    selected: overlayPanel.wholeRecording
                    onClicked: {
                        if (overlayPanel.wholeRecording) {
                            const start = Math.min(videoPlayer.position, Math.max(0, timelineArea.dur - 3000))
                            backend.setOverlayRange(backend.selectedOverlay, start, Math.min(timelineArea.dur, start + 3000))
                        } else {
                            backend.setOverlayRange(backend.selectedOverlay, 0, -1)
                        }
                    }
                }
                Column {
                    width: parent.width
                    spacing: 4
                    visible: !overlayPanel.wholeRecording
                    Text { text: qsTr("Overlay start"); color: root.textSecondary; font.pixelSize: SemanticTokens.fontSizeCaption }
                TimeField {
                    width: parent.width
                    id: overlayStartField
                    objectName: "previewOverlayStartField"
                    accessibleName: qsTr("Overlay start")
                    timeMs: root.selectedOverlayData ? root.selectedOverlayData.start : 0
                    onCommitted: function(ms) {
                        invalid = !backend.setOverlayRange(backend.selectedOverlay, ms, root.selectedOverlayData.end)
                    }
                }
                }
                Column {
                    width: parent.width
                    spacing: 4
                    visible: !overlayPanel.wholeRecording
                    Text { text: qsTr("Overlay end"); color: root.textSecondary; font.pixelSize: SemanticTokens.fontSizeCaption }
                TimeField {
                    width: parent.width
                    id: overlayEndField
                    objectName: "previewOverlayEndField"
                    accessibleName: qsTr("Overlay end")
                    timeMs: root.selectedOverlayData && root.selectedOverlayData.end >= 0 ? root.selectedOverlayData.end : timelineArea.dur
                    onCommitted: function(ms) {
                        invalid = !backend.setOverlayRange(backend.selectedOverlay, root.selectedOverlayData.start, ms)
                    }
                }
                }
                SegmentButton {
                    objectName: "previewOverlayDelete"
                    minimumButtonWidth: 0
                    text: qsTr("Delete")
                    tooltipText: qsTr("Delete this overlay")
                    onClicked: backend.removeOverlay(backend.selectedOverlay)
                }
                SegmentButton {
                    objectName: "previewOverlayDone"
                    minimumButtonWidth: 0
                    text: qsTr("Back to project")
                    tooltipText: qsTr("Drag the overlay to move it, or its corner to resize it. Done finishes editing.")
                    onClicked: {
                        if (overlayPanel.wholeRecording || root.commitPendingRange(overlayStartField, overlayEndField,
                                (start, end) => backend.setOverlayRange(backend.selectedOverlay, start, end)))
                            backend.selectedOverlay = -1
                    }
                }
            }

            // The two audio tracks (#189), in the contextual inspector.
            Column {
                id: audioPanel
                objectName: "previewAudioPanel"
                x: 12
                y: 8
                width: parent.width - 24
                spacing: SemanticTokens.spacing12
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
                    onActivated: function(index) { backend.microphoneGainDb = root.audioGainChoices[index] }
                }
                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewMicrophoneMute"
                    text: qsTr("Mute Microphone")
                    property string tooltipText: qsTr("Leave the microphone out of the export")
                    checked: backend.microphoneMuted
                    onClicked: backend.microphoneMuted = !backend.microphoneMuted
                }
                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewNoiseReduction"
                    enabled: !backend.microphoneMuted
                    text: qsTr("Reduce Noise")
                    property string tooltipText: qsTr("Turn down hiss, hum and room noise between words on the microphone")
                    checked: backend.microphoneNoiseReduction
                    onClicked: backend.microphoneNoiseReduction = !backend.microphoneNoiseReduction
                }
                EditorChoice {
                    objectName: "previewSystemAudioGain"
                    enabled: !backend.systemAudioMuted
                    tooltipText: qsTr("System audio volume in the export")
                    label: qsTr("System audio volume")
                    choices: root.audioGainChoices.map(v => root.gainLabel(v))
                    currentIndex: root.audioGainChoices.indexOf(backend.systemAudioGainDb)
                    onActivated: function(index) { backend.systemAudioGainDb = root.audioGainChoices[index] }
                }
                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewSystemAudioMute"
                    text: qsTr("Mute System Audio")
                    property string tooltipText: qsTr("Leave the system audio out of the export")
                    checked: backend.systemAudioMuted
                    onClicked: backend.systemAudioMuted = !backend.systemAudioMuted
                }
                SegmentButton {
                    objectName: "previewAudioDone"
                    minimumButtonWidth: 0
                    text: qsTr("Back to project")
                    tooltipText: qsTr("Back to the export options")
                    onClicked: root.audioOpen = false
                }
            }

            // Shortcut hints (#187), in the contextual inspector.
            Column {
                id: keysPanel
                objectName: "previewKeysPanel"
                x: 12
                y: 8
                width: parent.width - 24
                spacing: SemanticTokens.spacing12
                visible: root.selectedPart < 0 && root.keysOpen && backend.keyOverlayAvailable && !cropOverlay.editing
                         && !(backend.selectedPointerHide >= 0) && !(backend.selectedMask >= 0) && !(backend.selectedOverlay >= 0)
                         && !(backend.selectedZoom >= 0)

                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewKeysToggle"
                    text: backend.keyOverlayEnabled ? qsTr("Keys: On") : qsTr("Keys: Off")
                    property string tooltipText: qsTr("Draw the shortcuts into the exported video")
                    checked: backend.keyOverlayEnabled
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
                    choices: root.keyDisplayChoices.map(v => root.secondsLabel(v) + qsTr(" s"))
                    currentIndex: root.keyDisplayChoices.indexOf(backend.keyOverlayDisplayMs)
                    onActivated: function(index) { backend.keyOverlayDisplayMs = root.keyDisplayChoices[index] }
                }
                SegmentButton {
                    objectName: "previewKeysDone"
                    minimumButtonWidth: 0
                    text: qsTr("Back to project")
                    tooltipText: qsTr("Back to the export options")
                    onClicked: root.keysOpen = false
                }
            }

            // The frame around the export (#184), in the contextual inspector.
            Column {
                id: appearancePanel
                objectName: "previewAppearancePanel"
                x: 12
                y: 8
                width: parent.width - 24
                spacing: SemanticTokens.spacing12
                visible: root.selectedPart < 0 && root.appearanceOpen && backend.appearanceAvailable && !cropOverlay.editing
                         && !(backend.selectedPointerHide >= 0) && !(backend.selectedMask >= 0) && !(backend.selectedOverlay >= 0)
                         && !(backend.selectedZoom >= 0)

                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewAppearanceToggle"
                    text: backend.appearanceEnabled ? qsTr("Frame: On") : qsTr("Frame: Off")
                    property string tooltipText: qsTr("Draw the frame around the exported video")
                    checked: backend.appearanceEnabled
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
                    onClicked: backend.chooseAppearanceImage()
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
                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewAppearanceShadow"
                    text: qsTr("Shadow")
                    property string tooltipText: backend.appearanceShadow ? qsTr("Remove the shadow") : qsTr("Add a shadow under the video")
                    checked: backend.appearanceShadow
                    onClicked: backend.appearanceShadow = !backend.appearanceShadow
                }
                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewAppearanceBorder"
                    text: qsTr("Border")
                    property string tooltipText: backend.appearanceBorder ? qsTr("Remove the border") : qsTr("Add a thin border around the video")
                    checked: backend.appearanceBorder
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
                SegmentButton {
                    objectName: "previewAppearanceDone"
                    minimumButtonWidth: 0
                    text: qsTr("Back to project")
                    tooltipText: qsTr("Back to the export options")
                    onClicked: root.appearanceOpen = false
                }
            }

            // MP4 export options (#190), in the contextual inspector.
            Column {
                id: mp4Panel
                objectName: "previewMp4Panel"
                parent: exportPanel.optionsContent
                width: parent.width
                spacing: SemanticTokens.spacing8
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
                    choices: root.mp4FrameRateChoices.filter(v => v === 0 || v <= backend.mp4SourceFrameRate).map(v => v === 0 ? qsTr("Original") : v + " fps")
                    currentIndex: Math.max(0, root.mp4FrameRateChoices.filter(v => v === 0 || v <= backend.mp4SourceFrameRate).indexOf(backend.mp4FrameRate))
                    onActivated: function(index) { backend.mp4FrameRate = root.mp4FrameRateChoices.filter(v => v === 0 || v <= backend.mp4SourceFrameRate)[index] }
                }
                EditorChoice {
                    objectName: "previewMp4Quality"
                    tooltipText: qsTr("Default uses the quality from the Recording settings")
                    label: qsTr("Quality")
                    choices: root.mp4QualityLabels
                    currentIndex: backend.mp4Quality
                    onActivated: function(index) { backend.mp4Quality = index }
                }
                SegmentButton {
                    objectName: "previewMp4H264"
                    minimumButtonWidth: 0
                    text: "H.264"
                    tooltipText: qsTr("Plays everywhere")
                    selected: backend.mp4Codec === 0 || !backend.mp4HevcAvailable
                    onClicked: backend.mp4Codec = 0
                }
                SegmentButton {
                    objectName: "previewMp4Hevc"
                    enabled: backend.mp4HevcAvailable
                    minimumButtonWidth: 0
                    text: "HEVC"
                    tooltipText: backend.mp4HevcAvailable
                        ? qsTr("Smaller files at the same quality; some older players cannot open them")
                        : qsTr("HEVC export is not available on this system")
                    selected: backend.mp4Codec === 1 && backend.mp4HevcAvailable
                    onClicked: backend.mp4Codec = 1
                }
                Text {
                    objectName: "previewMp4Estimate"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                    visible: backend.mp4EstimateText.length > 0
                    text: qsTr("%1 × %2, %3 fps · %4 (estimate)")
                        .arg(backend.mp4OutputSize.width).arg(backend.mp4OutputSize.height)
                        .arg(backend.mp4OutputFrameRate).arg(backend.mp4EstimateText)
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeSmall
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

            }

            // Editing one range without a pointer, in the contextual inspector.
            Column {
                id: pointerHidePanel
                objectName: "previewPointerHidePanel"
                x: 12
                y: 8
                width: parent.width - 24
                spacing: SemanticTokens.spacing12
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
                    height: ComponentTokens.recordingPreviewControlButtonHeight
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("No pointer")
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeSmall
                }
                Column {
                    width: parent.width
                    spacing: 4
                    Text { text: qsTr("Hidden pointer start"); color: root.textSecondary; font.pixelSize: SemanticTokens.fontSizeCaption }
                TimeField {
                    width: parent.width
                    id: pointerHideStartField
                    objectName: "previewPointerHideStartField"
                    accessibleName: qsTr("Hidden pointer start")
                    timeMs: root.selectedPointerHideData ? root.selectedPointerHideData.start : 0
                    onCommitted: function(ms) {
                        invalid = !backend.setPointerHideRange(backend.selectedPointerHide, ms,
                                                               root.selectedPointerHideData.end)
                    }
                }
                }
                Column {
                    width: parent.width
                    spacing: 4
                    Text { text: qsTr("Hidden pointer end"); color: root.textSecondary; font.pixelSize: SemanticTokens.fontSizeCaption }
                TimeField {
                    width: parent.width
                    id: pointerHideEndField
                    objectName: "previewPointerHideEndField"
                    accessibleName: qsTr("Hidden pointer end")
                    timeMs: root.selectedPointerHideData ? root.selectedPointerHideData.end : 0
                    onCommitted: function(ms) {
                        invalid = !backend.setPointerHideRange(backend.selectedPointerHide,
                                                               root.selectedPointerHideData.start, ms)
                    }
                }
                }
                SegmentButton {
                    objectName: "previewPointerHideDelete"
                    minimumButtonWidth: 0
                    text: qsTr("Delete")
                    tooltipText: qsTr("Show the pointer here again")
                    onClicked: backend.removePointerHide(backend.selectedPointerHide)
                }
                SegmentButton {
                    objectName: "previewPointerHideDone"
                    minimumButtonWidth: 0
                    text: qsTr("Back to project")
                    tooltipText: qsTr("Finish editing this range")
                    onClicked: {
                        if (root.commitPendingRange(pointerHideStartField, pointerHideEndField,
                                (start, end) => backend.setPointerHideRange(backend.selectedPointerHide, start, end)))
                            backend.selectedPointerHide = -1
                    }
                }
            }

            // Editing one zoom segment, in the contextual inspector.
            Column {
                id: zoomPanel
                objectName: "previewZoomPanel"
                x: 12
                y: 8
                width: parent.width - 24
                spacing: SemanticTokens.spacing12
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

                Flow {
                    width: parent.width
                    spacing: 6
                Repeater {
                    model: root.zoomScales
                    SegmentButton {
                        required property int index
                        required property real modelData
                        objectName: "previewZoomScale" + index
                        minimumButtonWidth: 0
                        text: modelData.toLocaleString(Qt.locale(), "f", modelData % 1 ? 1 : 0) + "×"
                        tooltipText: qsTr("Zoom: %1").arg(text)
                        selected: zoomPanel.segment !== null && Math.abs(zoomPanel.segment.scale - modelData) < 0.05
                        onClicked: backend.setZoomScale(backend.selectedZoom, modelData)
                    }
                }
                }
                Column {
                    width: parent.width
                    spacing: 4
                    Text { text: qsTr("Zoom start"); color: root.textSecondary; font.pixelSize: SemanticTokens.fontSizeCaption }
                TimeField {
                    width: parent.width
                    id: zoomStartField
                    objectName: "previewZoomStartField"
                    accessibleName: qsTr("Zoom start")
                    timeMs: zoomPanel.segment ? zoomPanel.segment.start : 0
                    onCommitted: function(ms) {
                        invalid = !backend.setZoomRange(backend.selectedZoom, ms, zoomPanel.segment.end)
                    }
                }
                }
                Column {
                    width: parent.width
                    spacing: 4
                    Text { text: qsTr("Zoom end"); color: root.textSecondary; font.pixelSize: SemanticTokens.fontSizeCaption }
                TimeField {
                    width: parent.width
                    id: zoomEndField
                    objectName: "previewZoomEndField"
                    accessibleName: qsTr("Zoom end")
                    timeMs: zoomPanel.segment ? zoomPanel.segment.end : 0
                    onCommitted: function(ms) {
                        invalid = !backend.setZoomRange(backend.selectedZoom, zoomPanel.segment.start, ms)
                    }
                }
                }
                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewZoomReduceMotion"
                    text: qsTr("Less Motion")
                    property string tooltipText: qsTr("Gentler zooms for all segments: smaller and slower, no direct pans")
                    checked: backend.zoomReduceMotion
                    onClicked: backend.zoomReduceMotion = !backend.zoomReduceMotion
                }
                Switch {
                    width: parent.width
                    palette.windowText: root.textPrimary
                    Accessible.name: text
                    Accessible.description: tooltipText
                    objectName: "previewZoomEnabled"
                    text: backend.zoomEnabled ? qsTr("Zoom On") : qsTr("Zoom Off")
                    property string tooltipText: qsTr("Turn all zooms on or off without deleting them")
                    checked: backend.zoomEnabled
                    onClicked: backend.zoomEnabled = !backend.zoomEnabled
                }
                SegmentButton {
                    objectName: "previewZoomDelete"
                    minimumButtonWidth: 0
                    text: qsTr("Delete")
                    tooltipText: qsTr("Delete this zoom")
                    onClicked: backend.removeZoom(backend.selectedZoom)
                }
                SegmentButton {
                    objectName: "previewZoomDone"
                    minimumButtonWidth: 0
                    text: qsTr("Back to project")
                    tooltipText: qsTr("Click the video to move the zoom's centre. Done finishes editing.")
                    // Typed times are applied first; invalid ones keep the editor open.
                    onClicked: {
                        if (root.commitPendingRange(zoomStartField, zoomEndField,
                                (start, end) => backend.setZoomRange(backend.selectedZoom, start, end)))
                            backend.selectedZoom = -1
                    }
                }
            }

            // Exact crop entry while editing, in the contextual inspector so the
            // video keeps its size: aspect presets and X/Y/W/H in video pixels.
            Column {
                id: cropPanel
                objectName: "previewCropPanel"
                x: 12
                y: 8
                spacing: SemanticTokens.spacing4
                visible: cropOverlay.editing

                Flow {
                    width: controlRowArea.width - 24
                    spacing: 2
                    Repeater {
                        model: root.aspectPresetLabels
                        SegmentButton {
                            required property int index
                            required property string modelData
                            objectName: "previewAspectPreset" + index
                            minimumButtonWidth: 0
                            text: modelData
                            tooltipText: qsTr("Crop aspect: %1").arg(modelData)
                            selected: backend.cropAspectIndex === index
                            onClicked: root.chooseAspect(index)
                        }
                    }
                }

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
                        spacing: 2
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: root.textSecondary
                            font.pixelSize: SemanticTokens.fontSizeCaption
                            font.family: SemanticTokens.fontFamily
                        }
                        property alias field: cropNumberField
                        NumberField {
                            id: cropNumberField
                            objectName: "previewCrop_" + modelData.key
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


        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            color: root.bgPanel

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: root.borderColor
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: SemanticTokens.spacing12
                anchors.rightMargin: SemanticTokens.spacing12
                spacing: SemanticTokens.spacing8

                IconButton {
                    iconSource: videoPlayer.playing ? "qrc:/icons/icons/pause.svg" : "qrc:/icons/icons/play.svg"
                    tooltipText: videoPlayer.playing ? qsTr("Pause Preview (Space)") : qsTr("Play Preview (Space)")
                    onClicked: videoPlayer.togglePlayPause()
                }

                Text {
                    text: backend.formatTime(videoPlayer.position) + " / " + backend.formatTime(videoPlayer.duration)
                    elide: Text.ElideRight
                    Layout.maximumWidth: 140
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                    font.weight: SemanticTokens.fontWeightMedium
                    color: root.textPrimary
                    Layout.leftMargin: SemanticTokens.spacing4
                }

                EditorChoice {
                    objectName: "previewPlaybackSpeed"
                    width: 110
                    label: qsTr("Preview speed")
                    choices: root.speedOptions.map(v => root.speedLabel(v))
                    currentIndex: root.speedIndex
                    onActivated: function(index) { root.speedIndex = index; root.applyPlaybackRate() }
                }

                Item { Layout.fillWidth: true }

                IconButton {
                    objectName: "previewMuteButton"
                    visible: videoPlayer.audioPlaybackSupported
                    iconSource: videoPlayer.muted ? "qrc:/icons/icons/volume-x.svg" : "qrc:/icons/icons/volume-2.svg"
                    tooltipText: videoPlayer.muted ? qsTr("Unmute Preview (M)") : qsTr("Mute Preview (M)")
                    onClicked: videoPlayer.muted = !videoPlayer.muted
                }

                IconButton {
                    objectName: "previewUndoButton"
                    iconSource: "qrc:/icons/icons/undo.svg"
                    enabled: backend.canUndo
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    tooltipText: qsTr("Undo Edit (Ctrl+Z)")
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Undo Edit")
                    onClicked: root.undoEdit()
                }

                IconButton {
                    objectName: "previewRedoButton"
                    iconSource: "qrc:/icons/icons/redo.svg"
                    enabled: backend.canRedo
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    tooltipText: qsTr("Redo Edit (Ctrl+Shift+Z)")
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Redo Edit")
                    onClicked: root.redoEdit()
                }

                IconButton {
                    objectName: "previewResetButton"
                    iconSource: "qrc:/icons/icons/reset.svg"
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Reset Crop and Trim")
                    enabled: backend.hasEdits
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    tooltipText: qsTr("Reset Crop and Trim")
                    onClicked: {
                        cropOverlay.cancel()
                        backend.resetEdits()
                    }
                }

                IconButton {
                    objectName: "previewCompareButton"
                    iconSource: "qrc:/icons/icons/compare.svg"
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Compare with Original")
                    enabled: backend.hasCrop
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    highlighted: root.comparingOriginal
                    tooltipText: qsTr("Compare with Original (\\)")
                    onClicked: root.comparingOriginal = !root.comparingOriginal
                }

                IconButton {
                    objectName: "previewCropButton"
                    iconSource: "qrc:/icons/icons/crop.svg"
                    enabled: root.cropAvailable && !backend.isProcessing
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    highlighted: backend.hasCrop || cropOverlay.editing
                    tooltipText: cropOverlay.editing ? qsTr("Apply Crop (Enter) · Arrow keys move it by 2 px (Shift: 10 px)")
                                                     : backend.hasCrop ? qsTr("Edit Crop") : qsTr("Crop Recording")
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
                    iconSource: "qrc:/icons/icons/scissors.svg"
                    highlighted: backend.hasTrim
                    tooltipText: backend.hasTrim ? qsTr("Clear Trim Selection") : qsTr("Trim Recording")
                    onClicked: backend.toggleTrim()
                }

                Rectangle {
                    width: 1
                    Layout.preferredHeight: 24
                    color: root.borderColor
                }

                IconButton {
                    iconSource: "qrc:/icons/icons/trash-2.svg"
                    destructive: true
                    objectName: "previewDiscardButton"
                    tooltipText: backend.isProject ? qsTr("Close Without Saving (Esc)") : qsTr("Discard Recording (Esc)")
                    onClicked: backend.requestDiscard()
                }


            }
        }
    }

    RecordingExportPanel {
        id: exportPanel
        parent: root
        detailedOptions: backend.selectedFormat === 0
        onExportRequested: root.saveWithCrop()
        onReturnRequested: {
            if (!mp4WidthField.visible || mp4WidthField.commitText()) exportPanel.close()
        }
        Row {
            parent: exportPanel.formatContent
            spacing: 8
                SegmentButton {
                    text: "MP4"
                    tooltipText: qsTr("Export as MP4")
                    selected: backend.selectedFormat === 0
                    onClicked: backend.selectedFormat = 0
                }
                SegmentButton {
                    text: "GIF"
                    tooltipText: qsTr("Export as GIF")
                    selected: backend.selectedFormat === 1
                    onClicked: backend.selectedFormat = 1
                }
                SegmentButton {
                    text: "WebP"
                    tooltipText: qsTr("Export as WebP")
                    selected: backend.selectedFormat === 2
                    onClicked: backend.selectedFormat = 2
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

        Column {
            anchors.centerIn: parent
            spacing: SemanticTokens.spacing16

            Rectangle {
                width: 280
                height: 6
                radius: 3
                color: root.bgPanel
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
                const current = root.Window.activeFocusItem
                focusBeforeOpen = current !== root ? current : null
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
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: SemanticTokens.spacing16
        height: visible ? errorRow.implicitHeight + SemanticTokens.spacing16 : 0
        visible: backend.errorMessage !== ""
        radius: SemanticTokens.radiusSmall
        color: ComponentTokens.recordingPreviewDangerHover
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
                color: SemanticTokens.statusError
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
            if (backend.selectedZoom >= 0 || backend.selectedMask >= 0
                    || backend.selectedOverlay >= 0 || backend.selectedPointerHide >= 0 || root.selectedPart >= 0)
                root.openSection("")
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
            } else if (backend.timelineEditsAvailable && root.playheadPartData !== null && backend.timelineParts.length > 1) {
                backend.setPartDeleted(root.playheadPart, !root.playheadPartData.deleted)
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

        width: 48
        height: 24
        padding: 2
        horizontalAlignment: TextInput.AlignRight
        font.pixelSize: SemanticTokens.fontSizeCaption
        color: root.textPrimary
        selectByMouse: true
        validator: IntValidator { bottom: 0; top: 100000 }
        text: value
        Accessible.name: accessibleName
        background: Rectangle {
            radius: SemanticTokens.radiusSmall
            color: root.bgPanel
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
            edited = false
            text = Qt.binding(function() { return numberField.value })
            root.forceActiveFocus()
            event.accepted = true
        }
    }

    // Time entry ("m:ss.mmm"). committed returns false to reject the value.
    component TimeField: TextField {
        id: timeField
        property real timeMs: 0
        property string accessibleName: ""
        property bool invalid: false
        signal committed(real ms)

        width: 76
        height: 22
        padding: 2
        horizontalAlignment: TextInput.AlignHCenter
        font.pixelSize: SemanticTokens.fontSizeSmall
        color: invalid ? SemanticTokens.statusError : root.textPrimary
        selectByMouse: true
        text: backend.formatPreciseTime(timeMs)
        Accessible.name: accessibleName
        background: Rectangle {
            radius: SemanticTokens.radiusSmall
            color: root.bgPanel
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
        Accessible.role: Accessible.Button
        Accessible.name: tooltipText
        Accessible.onPressAction: if (enabled) clicked()

        signal clicked()
        activeFocusOnTab: true
        Keys.onSpacePressed: if (enabled) clicked()
        Keys.onReturnPressed: if (enabled) clicked()
        Keys.onEnterPressed: if (enabled) clicked()
        FocusFrame { anchors.fill: parent; showFocus: parent.activeFocus }

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

    component SegmentButton: Rectangle {
        property string text: ""
        property string tooltipText: ""
        property bool tooltipPreferredAbove: true
        property bool selected: false
        property bool primary: false
        property real minimumButtonWidth: 64

        signal clicked()
        activeFocusOnTab: true
        Keys.onSpacePressed: if (enabled) clicked()
        Keys.onReturnPressed: if (enabled) clicked()
        Keys.onEnterPressed: if (enabled) clicked()
        FocusFrame { anchors.fill: parent; showFocus: parent.activeFocus }

        function updateTooltip() {
            if (!fmtMouseArea.containsMouse || tooltipText === "") {
                root.hideButtonTooltip(this)
                return
            }

            root.showButtonTooltip(tooltipText, this, tooltipPreferredAbove)
        }

        width: Math.max(minimumButtonWidth, fmtLabel.implicitWidth + 2 * SemanticTokens.spacing16)
        height: ComponentTokens.recordingPreviewControlButtonHeight
        radius: SemanticTokens.radiusMedium
        opacity: enabled ? 1.0 : root.disabledControlOpacity

        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: tooltipText
        Accessible.checked: selected
        Accessible.onPressAction: if (enabled) clicked()

        color: primary ? root.accent : selected ? ComponentTokens.editorSelection
             : fmtMouseArea.pressed ? root.bgPanelPressed
             : fmtMouseArea.containsMouse ? root.bgPanelHover
             : root.bgPanel

        border.width: 1
        border.color: root.borderColor

        Behavior on color {
            ColorAnimation { duration: SemanticTokens.durationFast }
        }

        Text {
            id: fmtLabel
            anchors.centerIn: parent
            text: parent.text
            font.pixelSize: SemanticTokens.fontSizeSmall
            font.family: SemanticTokens.fontFamily
            font.weight: SemanticTokens.fontWeightSemiBold
            color: primary ? ComponentTokens.recordingPreviewPrimaryButtonIcon : selected ? root.accent : root.textPrimary
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
