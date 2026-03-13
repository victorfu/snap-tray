import QtQuick
import SnapTrayQml

Item {
    id: root
    focus: true

    property real selectionX: 0
    property real selectionY: 0
    property real selectionWidth: 0
    property real selectionHeight: 0
    property bool selectionComplete: false
    property bool isSelecting: false
    property point dragStartPoint: Qt.point(0, 0)
    property point currentPoint: Qt.point(0, 0)
    property point mousePosition: Qt.point(Math.floor(width / 2), Math.floor(height / 2))
    property int hoveredButtonId: -1

    property string idleHelpText: ""
    property string draggingHelpText: ""
    property string selectedHelpText: ""
    property string startButtonTooltip: ""
    property string cancelButtonTooltip: ""
    property string dimensionFormat: "%1 x %2"
    property int minimumSelectionSize: 10

    // ── Theme colors from ComponentTokens (Overlay — always dark) ──
    readonly property color themeGlassBg: ComponentTokens.recordingRegionGlassBg
    readonly property color themeGlassBgTop: ComponentTokens.recordingRegionGlassBgTop
    readonly property color themeHighlight: ComponentTokens.recordingRegionGlassHighlight
    readonly property color themeBorder: ComponentTokens.recordingRegionGlassBorder
    readonly property color themeText: ComponentTokens.recordingRegionText
    readonly property color themeHoverBg: ComponentTokens.recordingRegionHoverBg
    readonly property color themeIconNormal: ComponentTokens.recordingRegionIconNormal
    readonly property color themeIconCancel: ComponentTokens.recordingRegionIconCancel
    readonly property color themeIconRecord: ComponentTokens.recordingRegionIconRecord
    readonly property color overlayDimColor: ComponentTokens.recordingRegionOverlayDim

    readonly property bool hasSelection: selectionWidth > 0 && selectionHeight > 0
    readonly property real selectionRight: selectionX + selectionWidth - 1
    readonly property real selectionBottom: selectionY + selectionHeight - 1
    readonly property int selectionCenterX: Math.floor(selectionX + (selectionWidth - 1) / 2)
    readonly property string helpText: {
        if (selectionComplete)
            return selectedHelpText
        if (isSelecting)
            return draggingHelpText
        return idleHelpText
    }
    readonly property real actionBarWidth: ComponentTokens.recordingRegionActionBarWidth
    readonly property real actionBarHeight: ComponentTokens.recordingRegionActionBarHeight
    readonly property real actionBarY: {
        if (!selectionComplete || !hasSelection)
            return 0

        var bottomY = selectionBottom + 8 + actionBarHeight
        if (bottomY > height - 60)
            bottomY = selectionY - 8
        if (bottomY - actionBarHeight < 10)
            bottomY = selectionY + 10 + actionBarHeight
        return bottomY - actionBarHeight
    }
    readonly property string hoveredTooltipText: {
        if (hoveredButtonId === 0)
            return startButtonTooltip
        if (hoveredButtonId === 1)
            return cancelButtonTooltip
        return ""
    }

    signal confirmSelection(double x, double y, double width, double height)
    signal cancelSelection(bool hasSelection, double x, double y, double width, double height)

    function resetSelection() {
        selectionX = 0
        selectionY = 0
        selectionWidth = 0
        selectionHeight = 0
        selectionComplete = false
        isSelecting = false
        hoveredButtonId = -1
    }

    function startSelecting(x, y) {
        var startX = Math.round(x)
        var startY = Math.round(y)
        hoveredButtonId = -1
        selectionComplete = false
        isSelecting = true
        dragStartPoint = Qt.point(startX, startY)
        currentPoint = Qt.point(startX, startY)
        updateSelectionFromDrag()
    }

    function updateSelectionFromDrag() {
        var left = Math.max(0, Math.min(dragStartPoint.x, currentPoint.x))
        var top = Math.max(0, Math.min(dragStartPoint.y, currentPoint.y))
        var right = Math.min(width - 1, Math.max(dragStartPoint.x, currentPoint.x))
        var bottom = Math.min(height - 1, Math.max(dragStartPoint.y, currentPoint.y))

        selectionX = left
        selectionY = top
        selectionWidth = Math.max(0, right - left + 1)
        selectionHeight = Math.max(0, bottom - top + 1)
    }

    function containsSelectionPoint(x, y) {
        if (!hasSelection)
            return false

        return x >= selectionX && x <= selectionRight
            && y >= selectionY && y <= selectionBottom
    }

    function finalizeSelection() {
        if (!isSelecting)
            return

        isSelecting = false
        if (selectionWidth >= minimumSelectionSize && selectionHeight >= minimumSelectionSize) {
            selectionComplete = true
        } else {
            resetSelection()
        }
    }

    function requestCancel() {
        cancelSelection(selectionComplete && hasSelection,
                        selectionX, selectionY, selectionWidth, selectionHeight)
    }

    function requestConfirm() {
        if (!selectionComplete || !hasSelection)
            return

        confirmSelection(selectionX, selectionY, selectionWidth, selectionHeight)
    }

    function formattedDimensionText() {
        return dimensionFormat
            .replace("%1", selectionWidth)
            .replace("%2", selectionHeight)
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            requestCancel()
            event.accepted = true
            return
        }

        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && selectionComplete) {
            requestConfirm()
            event.accepted = true
        }
    }

    component GlassPanel: Item {
        id: glassPanel
        property int panelRadius: 6
        property color panelBg: root.themeGlassBg
        property color panelBgTop: root.themeGlassBgTop
        property color panelHighlight: root.themeHighlight
        property color panelBorder: root.themeBorder

        Rectangle {
            anchors.fill: parent
            radius: glassPanel.panelRadius
            gradient: Gradient {
                GradientStop { position: 0.0; color: glassPanel.panelBgTop }
                GradientStop { position: 1.0; color: glassPanel.panelBg }
            }
        }

        Item {
            anchors.fill: parent
            clip: true

            Rectangle {
                width: parent.width
                height: parent.height / 2
                radius: glassPanel.panelRadius

                gradient: Gradient {
                    GradientStop { position: 0.0; color: glassPanel.panelHighlight }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: glassPanel.panelRadius
            color: "transparent"
            border.width: 1
            border.color: glassPanel.panelBorder
        }
    }

    MouseArea {
        id: overlayArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: root.selectionComplete ? CursorTokens.defaultCursor : CursorTokens.captureSelection

        onPressed: function(mouse) {
            root.mousePosition = Qt.point(Math.round(mouse.x), Math.round(mouse.y))

            if (root.selectionComplete) {
                if (!root.containsSelectionPoint(mouse.x, mouse.y))
                    root.startSelecting(mouse.x, mouse.y)
                return
            }

            root.startSelecting(mouse.x, mouse.y)
        }

        onPositionChanged: function(mouse) {
            root.mousePosition = Qt.point(Math.round(mouse.x), Math.round(mouse.y))
            if (root.isSelecting) {
                root.currentPoint = Qt.point(Math.round(mouse.x), Math.round(mouse.y))
                root.updateSelectionFromDrag()
            }
        }

        onReleased: function(mouse) {
            root.mousePosition = Qt.point(Math.round(mouse.x), Math.round(mouse.y))
            root.finalizeSelection()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.overlayDimColor
        visible: !root.hasSelection || (!root.isSelecting && !root.selectionComplete)
    }

    Rectangle {
        x: 0
        y: 0
        width: root.width
        height: root.selectionY
        color: root.overlayDimColor
        visible: root.hasSelection && (root.isSelecting || root.selectionComplete)
    }

    Rectangle {
        x: 0
        y: root.selectionY + root.selectionHeight
        width: root.width
        height: root.height - y
        color: root.overlayDimColor
        visible: root.hasSelection && (root.isSelecting || root.selectionComplete)
    }

    Rectangle {
        x: 0
        y: root.selectionY
        width: root.selectionX
        height: root.selectionHeight
        color: root.overlayDimColor
        visible: root.hasSelection && (root.isSelecting || root.selectionComplete)
    }

    Rectangle {
        x: root.selectionX + root.selectionWidth
        y: root.selectionY
        width: root.width - x
        height: root.selectionHeight
        color: root.overlayDimColor
        visible: root.hasSelection && (root.isSelecting || root.selectionComplete)
    }

    Canvas {
        id: selectionCanvas
        anchors.fill: parent
        renderStrategy: Canvas.Threaded

        // Pre-extract token colors for Canvas rendering
        readonly property color crosshairColor: ComponentTokens.recordingRegionCrosshair
        readonly property color glowColor: ComponentTokens.recordingRegionGlowColor
        readonly property color gradStart: ComponentTokens.recordingRegionGradientStart
        readonly property color gradMid: ComponentTokens.recordingRegionGradientMid
        readonly property color gradEnd: ComponentTokens.recordingRegionGradientEnd
        readonly property color dashColor: ComponentTokens.recordingRegionDashColor

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            if (!root.isSelecting && !root.selectionComplete) {
                ctx.strokeStyle = crosshairColor
                ctx.lineWidth = 1

                ctx.beginPath()
                ctx.moveTo(0, root.mousePosition.y)
                ctx.lineTo(width, root.mousePosition.y)
                ctx.stroke()

                ctx.beginPath()
                ctx.moveTo(root.mousePosition.x, 0)
                ctx.lineTo(root.mousePosition.x, height)
                ctx.stroke()
                return
            }

            if (!root.hasSelection)
                return

            var x = root.selectionX + 0.5
            var y = root.selectionY + 0.5
            var w = Math.max(0, root.selectionWidth - 1)
            var h = Math.max(0, root.selectionHeight - 1)

            if (root.selectionComplete) {
                for (var i = 3; i >= 1; --i) {
                    var alpha = (25 / i) / 255
                    ctx.strokeStyle = Qt.rgba(glowColor.r, glowColor.g, glowColor.b, alpha)
                    ctx.lineWidth = 3 + i * 2
                    ctx.setLineDash([])
                    ctx.lineJoin = "miter"
                    ctx.strokeRect(x, y, w, h)
                }

                var gradient = ctx.createLinearGradient(x, y, x + w, y + h)
                gradient.addColorStop(0.0, gradStart)
                gradient.addColorStop(0.5, gradMid)
                gradient.addColorStop(1.0, gradEnd)
                ctx.strokeStyle = gradient
                ctx.lineWidth = 3
                ctx.lineJoin = "miter"
                ctx.strokeRect(x, y, w, h)
            } else {
                ctx.strokeStyle = dashColor
                ctx.lineWidth = 1
                ctx.setLineDash([4, 2])
                ctx.lineJoin = "miter"
                ctx.strokeRect(x, y, w, h)
                ctx.setLineDash([])
            }
        }
    }

    onMousePositionChanged: selectionCanvas.requestPaint()
    onSelectionXChanged: selectionCanvas.requestPaint()
    onSelectionYChanged: selectionCanvas.requestPaint()
    onSelectionWidthChanged: selectionCanvas.requestPaint()
    onSelectionHeightChanged: selectionCanvas.requestPaint()
    onSelectionCompleteChanged: selectionCanvas.requestPaint()
    onIsSelectingChanged: selectionCanvas.requestPaint()

    Item {
        id: dimensionLabel
        visible: root.hasSelection && (root.isSelecting || root.selectionComplete)
        width: Math.ceil(dimensionText.implicitWidth) + 16
        height: Math.ceil(dimensionText.implicitHeight) + 8
        x: Math.max(5, Math.min(root.selectionCenterX - Math.floor(width / 2), root.width - width - 5))
        y: {
            var aboveY = root.selectionY - height - 8
            if (aboveY < 5)
                return root.selectionBottom + 8
            return aboveY
        }

        GlassPanel {
            anchors.fill: parent
            panelRadius: ComponentTokens.recordingRegionDimensionRadius
        }

        Text {
            id: dimensionText
            anchors.centerIn: parent
            text: root.formattedDimensionText()
            color: root.themeText
            font.pixelSize: ComponentTokens.recordingRegionDimensionFontSize
            font.bold: true
        }
    }

    Item {
        id: helpLabel
        width: Math.ceil(helpTextItem.implicitWidth) + 24
        height: Math.ceil(helpTextItem.implicitHeight) + 12
        x: Math.floor((root.width - width) / 2)
        y: root.height - 40 - Math.floor(height / 2)

        GlassPanel {
            anchors.fill: parent
            panelRadius: ComponentTokens.recordingRegionGlassRadius
        }

        Text {
            id: helpTextItem
            anchors.centerIn: parent
            text: root.helpText
            color: root.themeText
            font.pixelSize: ComponentTokens.recordingRegionHelpFontSize
        }
    }

    Item {
        id: actionBar
        visible: root.selectionComplete && root.hasSelection
        width: root.actionBarWidth
        height: root.actionBarHeight
        x: root.selectionCenterX - Math.floor(width / 2)
        y: root.actionBarY
        z: 10

        GlassPanel {
            anchors.fill: parent
            panelRadius: ComponentTokens.recordingRegionActionBarRadius
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            cursorShape: CursorTokens.toolbarControl
        }

        Row {
            x: 10
            y: 4
            spacing: 2

            Item {
                width: ComponentTokens.recordingRegionActionBarButtonSize
                height: ComponentTokens.recordingRegionActionBarButtonHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: ComponentTokens.recordingRegionDimensionRadius
                    color: root.themeHoverBg
                    visible: startMouseArea.containsMouse
                }

                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/record.svg"
                    iconSize: ComponentTokens.recordingControlBarIconSize
                    color: root.themeIconRecord
                }

                MouseArea {
                    id: startMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: CursorTokens.toolbarControl

                    onPressed: function(mouse) {
                        mouse.accepted = true
                        root.requestConfirm()
                    }
                    onContainsMouseChanged: root.hoveredButtonId = containsMouse ? 0 : (root.hoveredButtonId === 0 ? -1 : root.hoveredButtonId)
                }
            }

            Item {
                width: ComponentTokens.recordingRegionActionBarButtonSize
                height: ComponentTokens.recordingRegionActionBarButtonHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: ComponentTokens.recordingRegionDimensionRadius
                    color: root.themeHoverBg
                    visible: cancelMouseArea.containsMouse
                }

                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/cancel.svg"
                    iconSize: ComponentTokens.recordingControlBarIconSize
                    color: cancelMouseArea.containsMouse ? root.themeIconCancel : root.themeIconNormal
                }

                MouseArea {
                    id: cancelMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: CursorTokens.toolbarControl

                    onPressed: function(mouse) {
                        mouse.accepted = true
                        root.requestCancel()
                    }
                    onContainsMouseChanged: root.hoveredButtonId = containsMouse ? 1 : (root.hoveredButtonId === 1 ? -1 : root.hoveredButtonId)
                }
            }
        }

        RecordingTooltip {
            id: actionTooltip
            visible: root.hoveredTooltipText !== ""
            tooltipText: root.hoveredTooltipText
            themeCornerRadius: ComponentTokens.recordingRegionGlassRadius
            z: 20

            x: {
                var buttonCenterX = root.hoveredButtonId === 0
                    ? 10 + 13
                    : 10 + 28 + 2 + 13
                var desiredX = buttonCenterX - Math.floor(width / 2)
                return Math.max(-actionBar.x + 5, Math.min(desiredX, root.width - actionBar.x - width - 5))
            }
            y: -height - 6
        }
    }
}
