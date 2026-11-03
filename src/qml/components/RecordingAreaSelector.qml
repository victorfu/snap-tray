import QtQuick
import SnapTrayQml

/**
 * RecordingAreaSelector.qml (#194)
 *
 * Chooses an area of one screen before recording. Recording-only: the
 * screenshot Region Selector is not involved. The capture stays full screen
 * in edit mode (the area is the preview's initial crop) and records the area
 * in quick mode. Drag to draw, drag inside to move; Enter records, Esc
 * cancels. Coordinates are logical, relative to the screen.
 */
Item {
    id: root

    // Set from C++.
    property real devicePixelRatio: 1.0
    property int minSide: 32 // logical; 64 video pixels at devicePixelRatio

    property real selX: 0
    property real selY: 0
    property real selW: 0
    property real selH: 0
    readonly property bool hasSelection: selW >= minSide && selH >= minSide

    // Index-aligned with ratioLabels; {0, 0} = free.
    property int ratioIndex: 0
    readonly property var ratios: [[0, 0], [16, 9], [4, 3], [1, 1], [9, 16]]
    readonly property var ratioLabels: [qsTr("Free"), "16:9", "4:3", "1:1", "9:16"]
    readonly property real ratio: ratios[ratioIndex][0] > 0 ? ratios[ratioIndex][0] / ratios[ratioIndex][1] : 0

    readonly property color dimColor: Qt.rgba(0, 0, 0, 0.45)
    readonly property int toolbarMargin: 24

    signal accepted(real x, real y, real width, real height)
    signal fullScreenChosen()
    signal cancelled()

    focus: true
    Keys.onEscapePressed: root.cancelled()
    Keys.onReturnPressed: root.confirm()
    Keys.onEnterPressed: root.confirm()

    function confirm() {
        if (hasSelection) accepted(selX, selY, selW, selH)
    }

    function chooseFullScreen() {
        fullScreenChosen()
    }

    function clampSelection() {
        selW = Math.max(minSide, Math.min(selW, width))
        selH = Math.max(minSide, Math.min(selH, height))
        selX = Math.max(0, Math.min(selX, width - selW))
        selY = Math.max(0, Math.min(selY, height - selH))
    }

    // The largest rect of the ratio inside the current one (or the screen).
    function applyRatio(index) {
        ratioIndex = index
        if (ratio <= 0) return
        const bounds = hasSelection ? Qt.rect(selX, selY, selW, selH) : Qt.rect(0, 0, width, height)
        let w = bounds.width
        let h = w / ratio
        if (h > bounds.height) {
            h = bounds.height
            w = h * ratio
        }
        selX = bounds.x + (bounds.width - w) / 2
        selY = bounds.y + (bounds.height - h) / 2
        selW = w
        selH = h
        clampSelection()
    }

    // Drawing from a point to another, kept to the ratio and on the screen.
    function drawTo(ax, ay, bx, by) {
        bx = Math.max(0, Math.min(bx, width))
        by = Math.max(0, Math.min(by, height))
        let w = Math.abs(bx - ax)
        let h = Math.abs(by - ay)
        if (ratio > 0) {
            if (w / Math.max(h, 1) > ratio) h = w / ratio
            else w = h * ratio
            const maxW = bx >= ax ? width - ax : ax
            const maxH = by >= ay ? height - ay : ay
            if (w > maxW) { w = maxW; h = w / ratio }
            if (h > maxH) { h = maxH; w = h * ratio }
        }
        selX = bx >= ax ? ax : ax - w
        selY = by >= ay ? ay : ay - h
        selW = w
        selH = h
    }

    // Dimmed outside the area (the whole screen before one is drawn).
    Rectangle { x: 0; y: 0; width: root.width; height: root.hasSelection || root.selW > 0 ? root.selY : root.height; color: root.dimColor }
    Rectangle { visible: root.selW > 0; x: 0; y: root.selY; width: root.selX; height: root.selH; color: root.dimColor }
    Rectangle { visible: root.selW > 0; x: root.selX + root.selW; y: root.selY; width: root.width - root.selX - root.selW; height: root.selH; color: root.dimColor }
    Rectangle { visible: root.selW > 0; x: 0; y: root.selY + root.selH; width: root.width; height: root.height - root.selY - root.selH; color: root.dimColor }

    Rectangle {
        objectName: "areaSelectionFrame"
        visible: root.selW > 0
        x: root.selX
        y: root.selY
        width: root.selW
        height: root.selH
        color: "transparent"
        border.width: 2
        border.color: DesignSystem.accentDefault
    }

    // The size the recording's crop will have, in video pixels.
    Rectangle {
        objectName: "areaSizeLabel"
        visible: root.selW > 0
        x: Math.min(root.selX, root.width - width)
        y: root.selY >= height + 6 ? root.selY - height - 6 : root.selY + 6
        width: sizeText.implicitWidth + 12
        height: sizeText.implicitHeight + 6
        radius: SemanticTokens.radiusSmall
        color: Qt.rgba(0, 0, 0, 0.7)
        Text {
            id: sizeText
            anchors.centerIn: parent
            text: Math.round(root.selW * root.devicePixelRatio) + " × " + Math.round(root.selH * root.devicePixelRatio)
            color: "white"
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
        }
    }

    MouseArea {
        id: drawArea
        objectName: "areaDrawArea"
        anchors.fill: parent
        cursorShape: CursorTokens.captureSelection
        property real anchorX: 0
        property real anchorY: 0
        property bool moving: false
        property real grabX: 0
        property real grabY: 0
        onPressed: function(mouse) {
            moving = root.hasSelection && mouse.x >= root.selX && mouse.x <= root.selX + root.selW
                && mouse.y >= root.selY && mouse.y <= root.selY + root.selH
            if (moving) {
                grabX = mouse.x - root.selX
                grabY = mouse.y - root.selY
            } else {
                anchorX = mouse.x
                anchorY = mouse.y
                root.selX = mouse.x
                root.selY = mouse.y
                root.selW = 0
                root.selH = 0
            }
        }
        onPositionChanged: function(mouse) {
            if (moving) {
                root.selX = Math.max(0, Math.min(mouse.x - grabX, root.width - root.selW))
                root.selY = Math.max(0, Math.min(mouse.y - grabY, root.height - root.selH))
            } else {
                root.drawTo(anchorX, anchorY, mouse.x, mouse.y)
            }
        }
        onReleased: {
            // A click (or a tiny drag) keeps no area.
            if (!moving && (root.selW < root.minSide || root.selH < root.minSide)) {
                if (root.selW < 4 && root.selH < 4) {
                    root.selW = 0
                    root.selH = 0
                } else {
                    root.clampSelection()
                }
            }
        }
        onDoubleClicked: root.confirm()
    }

    // The toolbar: ratio, full screen, cancel, record.
    Item {
        objectName: "areaToolbar"
        // One line where it fits; on a narrow screen (a portrait display at
        // 200%) the buttons wrap, so Record is always on screen (PR #205 review).
        readonly property real naturalWidth: {
            let total = 0
            let count = 0
            for (let i = 0; i < toolbarRow.children.length; ++i) {
                const child = toolbarRow.children[i]
                if (child.visible && child.implicitWidth > 0) {
                    total += child.implicitWidth
                    ++count
                }
            }
            return total + Math.max(0, count - 1) * toolbarRow.spacing
        }
        width: Math.min(naturalWidth, root.width - 2 * root.toolbarMargin - 16) + 16
        height: toolbarRow.implicitHeight + 12
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.toolbarMargin

        GlassSurface {
            anchors.fill: parent
            glassBg: ComponentTokens.captureOverlayPanelBg
            glassBgTop: ComponentTokens.captureOverlayPanelBgTop
            glassHighlight: ComponentTokens.captureOverlayPanelHighlight
            glassBorder: ComponentTokens.captureOverlayPanelBorder
            glassRadius: ComponentTokens.captureOverlayPanelRadius
        }

        Flow {
            id: toolbarRow
            anchors.centerIn: parent
            width: parent.width - 16
            spacing: 6

            Text {
                height: 28
                verticalAlignment: Text.AlignVCenter
                text: root.hasSelection ? qsTr("Drag to move or redraw") : qsTr("Drag to choose the area to record")
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeCaption
                font.family: SemanticTokens.fontFamily
                rightPadding: 6
            }

            Repeater {
                model: root.ratioLabels
                DialogButton {
                    required property int index
                    required property string modelData
                    objectName: "areaRatio" + index
                    height: 28
                    text: modelData
                    style: root.ratioIndex === index ? "primary" : "secondary"
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Ratio %1").arg(modelData)
                    onClicked: root.applyRatio(index)
                }
            }

            DialogButton {
                objectName: "areaFullScreenButton"
                height: 28
                text: qsTr("Full Screen")
                style: "secondary"
                onClicked: root.chooseFullScreen()
            }
            DialogButton {
                objectName: "areaCancelButton"
                height: 28
                text: qsTr("Cancel")
                style: "secondary"
                onClicked: root.cancelled()
            }
            DialogButton {
                objectName: "areaRecordButton"
                height: 28
                enabled: root.hasSelection
                text: qsTr("Record")
                style: "primary"
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Record this area")
                onClicked: root.confirm()
            }
        }
    }
}
