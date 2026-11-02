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
    readonly property color bgPanel: ComponentTokens.recordingPreviewPanel
    readonly property color bgPanelHover: ComponentTokens.recordingPreviewPanelHover
    readonly property color bgPanelPressed: ComponentTokens.recordingPreviewPanelPressed
    readonly property color borderColor: ComponentTokens.recordingPreviewBorder
    readonly property color trimOverlayColor: ComponentTokens.recordingPreviewTrimOverlay
    readonly property real disabledControlOpacity: 0.6
    readonly property real overlayChipHeight: 28
    readonly property real overlayChipButtonSize: 20

    property bool isScrubbing: false
    // Shows the unedited frame (no crop dimming) while on. Only meaningful
    // with a committed crop, and never while a crop is being edited.
    property bool comparingOriginal: false
    Connections {
        target: backend
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
        return backend.zoomViewportInView(videoPlayer.position, videoPlayer.contentRect)
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

    function nextPointerScale(current) {
        for (let i = 0; i < pointerScales.length; ++i) {
            if (pointerScales[i] > current + 0.01)
                return pointerScales[i]
        }
        return pointerScales[0]
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

    // The single guarded save path shared by the Save button, Ctrl+S and non-editing Enter.
    // A visible crop draft is applied first so the export matches what the user sees.
    function saveWithCrop() {
        if (backend.isProcessing)
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
        if (cropOverlay.editing && !root.applyCropWithPendingInput())
            return
        backend.save()
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
        cropOverlay.cancel()
        backend.undo()
    }
    function redoEdit() {
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
        gradient: Gradient {
            GradientStop { position: 0.0; color: ComponentTokens.recordingPreviewBgStart }
            GradientStop { position: 1.0; color: ComponentTokens.recordingPreviewBgEnd }
        }
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        enabled: !backend.isProcessing

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // A zoomed picture never spills over the controls. Only while
            // zoomed: crop handles at the edges must stay visible when editing.
            clip: root.zoomFactor > 1.001

            VideoPlaybackItem {
                id: videoPlayer
                objectName: "previewVideoPlayer"
                anchors.fill: parent
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
                }
                onStateChanged: backend.updatePlayingState(videoPlayer.playing)
                onErrorOccurred: function(message) { backend.reportPlaybackError(message) }
                Component.onCompleted: {
                    setLooping(true)
                    play()
                }
            }

            // The pointer and click effects the export will draw, drawn by
            // the same renderer; hidden while comparing with the original.
            RecordingCursorOverlay {
                objectName: "previewCursorOverlay"
                anchors.fill: parent
                z: 3
                visible: !root.comparingOriginal
                backend: root.previewBackend
                position: videoPlayer.position
                contentRect: videoPlayer.contentRect
                transform: [
                    Translate { x: -root.zoomView.x; y: -root.zoomView.y },
                    Scale { xScale: root.zoomFactor; yScale: root.zoomFactor },
                    Translate { x: root.zoomTarget.x; y: root.zoomTarget.y }
                ]
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

            Rectangle {
                id: playOverlay
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

        Item {
            id: timelineArea
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.leftMargin: SemanticTokens.spacing16
            Layout.rightMargin: SemanticTokens.spacing16

            readonly property real trackY: 16
            readonly property real trackHeight: 6
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

            Rectangle {
                id: trackRect
                x: timelineArea.handleWidth
                y: timelineArea.trackY
                width: timelineArea.width - 2 * timelineArea.handleWidth
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

            // Masks just above the track; click one to edit it.
            Repeater {
                model: backend.masks
                Rectangle {
                    required property int index
                    required property var modelData
                    objectName: "previewMaskSegment" + index
                    readonly property bool selected: backend.selectedMask === index
                    readonly property real endMs: modelData.end < 0 ? timelineArea.dur : modelData.end
                    x: timelineArea.posToX(modelData.start)
                    y: 10
                    z: 2
                    width: Math.max(4, timelineArea.posToX(endMs) - x)
                    height: 5
                    radius: 2
                    color: root.textPrimary
                    opacity: selected ? 0.9 : 0.45
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Mask from %1 to %2").arg(backend.formatPreciseTime(modelData.start))
                                         .arg(modelData.end < 0 ? qsTr("the end") : backend.formatPreciseTime(modelData.end))
                    Accessible.onPressAction: backend.selectedMask = index
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -2
                        cursorShape: CursorTokens.clickable
                        onClicked: {
                            backend.selectedZoom = -1
                            backend.selectedMask = parent.selected ? -1 : parent.index
                            if (backend.selectedMask >= 0) {
                                videoPlayer.pause()
                                videoPlayer.seek(parent.modelData.start)
                            }
                        }
                    }
                }
            }

            // Zoom segments above the track; click one to edit it.
            Repeater {
                model: backend.zoomSegments
                Rectangle {
                    required property int index
                    required property var modelData
                    objectName: "previewZoomSegment" + index
                    readonly property bool selected: backend.selectedZoom === index
                    x: timelineArea.posToX(modelData.start)
                    y: 1
                    z: 2
                    width: Math.max(4, timelineArea.posToX(modelData.end) - x)
                    height: 7
                    radius: 3
                    color: root.accent
                    opacity: !backend.zoomEnabled ? 0.3 : selected ? 1.0 : 0.6
                    border.width: selected ? 1 : 0
                    border.color: root.textPrimary

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Zoom %1× from %2 to %3").arg(modelData.scale.toFixed(1))
                                         .arg(backend.formatPreciseTime(modelData.start))
                                         .arg(backend.formatPreciseTime(modelData.end))
                    Accessible.onPressAction: backend.selectedZoom = index

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -2
                        cursorShape: CursorTokens.clickable
                        onClicked: {
                            backend.selectedMask = -1
                            backend.selectedZoom = parent.selected ? -1 : parent.index
                            if (backend.selectedZoom >= 0) {
                                videoPlayer.pause()
                                // Show it fully zoomed, a moment after it starts.
                                videoPlayer.seek(Math.min(parent.modelData.end, parent.modelData.start + 100))
                            }
                        }
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
                anchors.fill: parent
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
                anchors.top: trackRect.bottom
                anchors.topMargin: 6
                text: backend.formatTime(videoPlayer.position)
                font.pixelSize: SemanticTokens.fontSizeSmall
                font.family: SemanticTokens.fontFamily
                color: root.textSecondary
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: timelineArea.handleWidth
                anchors.top: trackRect.bottom
                anchors.topMargin: 6
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
                anchors.top: trackRect.bottom
                anchors.topMargin: 6
                width: Math.max(0, parent.width - 2 * 180)
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
                anchors.top: trackRect.bottom
                anchors.topMargin: 2
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
            Layout.fillWidth: true
            // Rows wrap at narrow widths (down to the 640 px minimum): grow with
            // them. The format row always keeps its size (hidden by opacity
            // while a panel replaces it), so opening the crop or zoom editor
            // never resizes the video under the pointer.
            Layout.preferredHeight: Math.max(formatRow.height,
                                             zoomPanel.visible ? zoomPanel.height : 0,
                                             maskPanel.visible ? maskPanel.height : 0,
                                             cropPanel.visible ? cropPanel.height : 0,
                                             ComponentTokens.recordingPreviewControlButtonHeight)
                                    + SemanticTokens.spacing16
            Flow {
                id: formatRow
                objectName: "previewFormatRow"
                anchors.centerIn: parent
                width: Math.min(controlRowArea.width - 2 * SemanticTokens.spacing8, root.rowContentWidth(this))
                spacing: SemanticTokens.spacing8
                readonly property bool shown: !cropOverlay.editing && !(backend.selectedZoom >= 0)
                                              && !(backend.selectedMask >= 0)
                opacity: shown ? 1 : 0
                enabled: shown
                Accessible.ignored: !shown

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

                // Pointer effects, only for recordings with pointer data.
                Item {
                    width: SemanticTokens.spacing8
                    height: 1
                    visible: backend.hasPointerData
                }
                SegmentButton {
                    objectName: "previewPointerToggle"
                    visible: backend.hasPointerData
                    enabled: backend.cursorEffectsAvailable
                    text: qsTr("Pointer")
                    tooltipText: backend.showCursor ? qsTr("Hide the Mouse Pointer") : qsTr("Show the Mouse Pointer")
                    selected: backend.showCursor && backend.cursorEffectsAvailable
                    onClicked: backend.showCursor = !backend.showCursor
                }
                SegmentButton {
                    objectName: "previewPointerSize"
                    visible: backend.hasPointerData
                    enabled: backend.cursorEffectsAvailable && backend.showCursor
                    minimumButtonWidth: 0
                    text: Number(backend.cursorScale).toLocaleString(Qt.locale(), "f", backend.cursorScale % 1 ? 1 : 0) + "×"
                    tooltipText: qsTr("Pointer Size: %1").arg(text)
                    onClicked: backend.cursorScale = root.nextPointerScale(backend.cursorScale)
                }
                SegmentButton {
                    objectName: "previewPointerSmoothing"
                    visible: backend.hasPointerData
                    enabled: backend.cursorEffectsAvailable && backend.showCursor
                    minimumButtonWidth: 0
                    text: root.pointerSmoothingLabels[backend.cursorSmoothing]
                    tooltipText: qsTr("Smooths the pointer's path; clicks stay where they happened")
                    onClicked: backend.cursorSmoothing = (backend.cursorSmoothing + 1) % root.pointerSmoothingLabels.length
                }
                SegmentButton {
                    objectName: "previewClickRipple"
                    visible: backend.hasPointerData
                    enabled: backend.clickEffectsAvailable
                    text: qsTr("Clicks")
                    tooltipText: backend.clickRipple ? qsTr("Hide Click Highlights") : qsTr("Highlight Clicks")
                    selected: backend.clickRipple && backend.clickEffectsAvailable
                    onClicked: backend.clickRipple = !backend.clickRipple
                }

                // Zoom and pan.
                Item {
                    width: SemanticTokens.spacing8
                    height: 1
                    visible: backend.zoomAvailable
                }
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
                        const count = backend.generateAutoZoom()
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
                        videoPlayer.pause()
                        backend.selectedZoom = -1
                        backend.addMask()
                    }
                }
            }

            // Editing one mask, in place of the format buttons.
            Flow {
                id: maskPanel
                objectName: "previewMaskPanel"
                anchors.centerIn: parent
                width: Math.min(controlRowArea.width - 2 * SemanticTokens.spacing8, root.rowContentWidth(this))
                spacing: SemanticTokens.spacing4
                visible: backend.selectedMask >= 0 && !cropOverlay.editing
                readonly property bool wholeRecording: root.selectedMaskData !== null
                    && root.selectedMaskData.start === 0 && root.selectedMaskData.end < 0

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
                TimeField {
                    objectName: "previewMaskStartField"
                    visible: !maskPanel.wholeRecording
                    accessibleName: qsTr("Mask start")
                    timeMs: root.selectedMaskData ? root.selectedMaskData.start : 0
                    onCommitted: function(ms) {
                        invalid = !backend.setMaskRange(backend.selectedMask, ms, root.selectedMaskData.end)
                    }
                }
                TimeField {
                    objectName: "previewMaskEndField"
                    visible: !maskPanel.wholeRecording
                    accessibleName: qsTr("Mask end")
                    timeMs: root.selectedMaskData && root.selectedMaskData.end >= 0 ? root.selectedMaskData.end : timelineArea.dur
                    onCommitted: function(ms) {
                        invalid = !backend.setMaskRange(backend.selectedMask, root.selectedMaskData.start, ms)
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
                    text: qsTr("Done")
                    tooltipText: qsTr("Drag the mask to move it, or its corner to resize it. Done finishes editing.")
                    onClicked: backend.selectedMask = -1
                }
            }

            // Editing one zoom segment, in place of the format buttons.
            Flow {
                id: zoomPanel
                objectName: "previewZoomPanel"
                anchors.centerIn: parent
                width: Math.min(controlRowArea.width - 2 * SemanticTokens.spacing8, root.rowContentWidth(this))
                spacing: SemanticTokens.spacing4
                visible: backend.selectedZoom >= 0 && !(backend.selectedMask >= 0) && !cropOverlay.editing
                readonly property var segment: backend.selectedZoom >= 0 ? backend.zoomSegments[backend.selectedZoom] : null

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
                TimeField {
                    objectName: "previewZoomStartField"
                    accessibleName: qsTr("Zoom start")
                    timeMs: zoomPanel.segment ? zoomPanel.segment.start : 0
                    onCommitted: function(ms) {
                        invalid = !backend.setZoomRange(backend.selectedZoom, ms, zoomPanel.segment.end)
                    }
                }
                TimeField {
                    objectName: "previewZoomEndField"
                    accessibleName: qsTr("Zoom end")
                    timeMs: zoomPanel.segment ? zoomPanel.segment.end : 0
                    onCommitted: function(ms) {
                        invalid = !backend.setZoomRange(backend.selectedZoom, zoomPanel.segment.start, ms)
                    }
                }
                SegmentButton {
                    objectName: "previewZoomReduceMotion"
                    minimumButtonWidth: 0
                    text: qsTr("Less Motion")
                    tooltipText: qsTr("Gentler zooms for all segments: smaller and slower, no direct pans")
                    selected: backend.zoomReduceMotion
                    onClicked: backend.zoomReduceMotion = !backend.zoomReduceMotion
                }
                SegmentButton {
                    objectName: "previewZoomEnabled"
                    minimumButtonWidth: 0
                    text: backend.zoomEnabled ? qsTr("Zoom On") : qsTr("Zoom Off")
                    tooltipText: qsTr("Turn all zooms on or off without deleting them")
                    selected: backend.zoomEnabled
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
                    text: qsTr("Done")
                    tooltipText: qsTr("Click the video to move the zoom's centre. Done finishes editing.")
                    onClicked: backend.selectedZoom = -1
                }
            }

            // Exact crop entry while editing, in place of the format buttons so the
            // video keeps its size: aspect presets and X/Y/W/H in video pixels.
            Row {
                id: cropPanel
                objectName: "previewCropPanel"
                anchors.centerIn: parent
                spacing: SemanticTokens.spacing4
                visible: cropOverlay.editing

                Row {
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
                        anchors.verticalCenter: parent.verticalCenter
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
                                root.setDraftFromFields(modelData.key === "x" ? v : r.x,
                                                        modelData.key === "y" ? v : r.y,
                                                        modelData.key === "width" ? v : r.width,
                                                        modelData.key === "height" ? v : r.height)
                            }
                        }
                    }
                }
            }
        }


        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
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
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                    font.weight: SemanticTokens.fontWeightMedium
                    color: root.textPrimary
                    Layout.leftMargin: SemanticTokens.spacing4
                }

                IconButton {
                    iconText: root.speedOptions[root.speedIndex] + "x"
                    tooltipText: qsTr("Playback Speed: %1x").arg(root.speedOptions[root.speedIndex])
                    onClicked: {
                        root.speedIndex = (root.speedIndex + 1) % root.speedOptions.length
                        videoPlayer.playbackRate = root.speedOptions[root.speedIndex]
                    }
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
                    iconText: "↶"
                    enabled: backend.canUndo
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    tooltipText: qsTr("Undo Edit (Ctrl+Z)")
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Undo Edit")
                    onClicked: root.undoEdit()
                }

                IconButton {
                    objectName: "previewRedoButton"
                    iconText: "↷"
                    enabled: backend.canRedo
                    opacity: enabled ? 1.0 : root.disabledControlOpacity
                    tooltipText: qsTr("Redo Edit (Ctrl+Shift+Z)")
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Redo Edit")
                    onClicked: root.redoEdit()
                }

                IconButton {
                    objectName: "previewResetButton"
                    iconText: "⟲"
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
                    iconText: "◧"
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
                        else
                            cropOverlay.beginEditing()
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
                    tooltipText: qsTr("Discard Recording (Esc)")
                    onClicked: backend.requestDiscard()
                }

                IconButton {
                    objectName: "previewSaveButton"
                    iconSource: "qrc:/icons/icons/save.svg"
                    primary: true
                    tooltipText: qsTr("Save Recording (Enter / Ctrl+S)")
                    onClicked: root.saveWithCrop()
                }
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
        onVisibleChanged: if (visible) focusIndex = 0

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
                    text: qsTr("Discard this recording?")
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    font.weight: SemanticTokens.fontWeightMedium
                    color: root.textPrimary
                }

                Text {
                    id: confirmBody
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("The video will be deleted and cannot be recovered.")
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                    color: root.textSecondary
                }

                Row {
                    anchors.right: parent.right
                    spacing: SemanticTokens.spacing8

                    DialogButton {
                        objectName: "discardKeepButton"
                        text: qsTr("Keep Recording")
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
                        text: qsTr("Discard")
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
                    root.saveWithCrop()
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
        // Keys a text field did not use (for example Left at its first
        // character) must not seek, play or discard.
        if (root.textEntryActive)
            return

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
            backend.requestDiscard()
            event.accepted = true
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.saveWithCrop()
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
                root.saveWithCrop()
                event.accepted = true
            }
            break
        case Qt.Key_BracketLeft:
            if (root.speedIndex > 0) {
                root.speedIndex--
                videoPlayer.playbackRate = root.speedOptions[root.speedIndex]
            }
            event.accepted = true
            break
        case Qt.Key_BracketRight:
            if (root.speedIndex < root.speedOptions.length - 1) {
                root.speedIndex++
                videoPlayer.playbackRate = root.speedOptions[root.speedIndex]
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
        onEditingFinished: commitText()
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
        onEditingFinished: commitText()
        onTextEdited: {
            invalid = false
            edited = true
        }
        Keys.onEscapePressed: function(event) {
            invalid = false
            edited = false
            text = backend.formatPreciseTime(timeMs)
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

        signal clicked()

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
                return ComponentTokens.recordingPreviewControlHighlight
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
        property real minimumButtonWidth: 64

        signal clicked()

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

        color: selected ? (fmtMouseArea.containsMouse ? root.accentHover : root.accent)
             : fmtMouseArea.pressed ? root.bgPanelPressed
             : fmtMouseArea.containsMouse ? root.bgPanelHover
             : root.bgPanel

        border.width: selected ? 0 : 1
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
            color: selected ? ComponentTokens.recordingPreviewPrimaryButtonIcon : root.textSecondary
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
