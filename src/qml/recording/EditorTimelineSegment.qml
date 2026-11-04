import QtQuick
import SnapTrayQml

// A draft is local until release: one backend mutation and one undo step per drag.
Rectangle {
    id: root
    property real startMs: 0
    property real endMs: 0
    // Display an open end at the duration, but preserve its persisted sentinel.
    property bool openEnded: false
    readonly property real resizeHandleWidth: Math.min(6, width / 4)
    property real lowerMs: 0
    property real upperMs: 1
    property real minimumDuration: 100
    property real pixelsPerMs: 1
    property real trackX: 0
    property bool selected: false
    property string label: ""
    // Spoken name; the visible label stays short (exact times live in the inspector).
    property string accessibleName: label
    // Each effect kind has its own fill, edge and icon, so lanes differ by more than hue.
    property color fillColor: ComponentTokens.editorSurface
    property color edgeColor: ComponentTokens.editorBorder
    property url iconSource: ""
    property var prepareSelection: function() { return true }
    signal activated()
    signal rangeEdited(real start, real end)
    property bool dragging: false
    property real draftStart: startMs
    property real draftEnd: endMs
    x: trackX + (dragging ? draftStart : startMs) * pixelsPerMs
    width: Math.max(8, ((dragging ? draftEnd : endMs) - (dragging ? draftStart : startMs)) * pixelsPerMs)
    height: 24
    radius: 5
    color: fillColor
    border.color: edgeColor
    border.width: selected ? 2 : 1
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.selected: selected
    Accessible.onPressAction: if (prepareSelection()) activated()
    Keys.onReturnPressed: if (prepareSelection()) activated()
    Keys.onSpacePressed: if (prepareSelection()) activated()
    Keys.onEscapePressed: function(event) {
        if (dragging) dragging = false
        else event.accepted = false
    }
    Row {
        id: content
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 16
        spacing: 5
        visible: parent.width > 20
        clip: true
        SvgIcon {
            id: kindIcon
            anchors.verticalCenter: parent.verticalCenter
            visible: root.iconSource != "" && root.width > 40
            source: root.iconSource
            iconSize: 13
            color: SemanticTokens.textPrimary
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: content.width - (kindIcon.visible ? kindIcon.width + content.spacing : 0)
            text: root.label
            color: SemanticTokens.textPrimary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.weight: root.selected ? SemanticTokens.fontWeightMedium : SemanticTokens.fontWeightRegular
            elide: Text.ElideRight
        }
    }
    // Resize grips on the selected segment.
    Repeater {
        model: root.selected && root.width >= 16 ? 2 : 0
        Rectangle {
            required property int index
            x: index === 0 ? -2 : root.width - width + 2
            anchors.verticalCenter: parent.verticalCenter
            width: 5
            height: root.height - 6
            radius: 2
            color: root.edgeColor
            border.width: 1
            border.color: ComponentTokens.editorSurface
        }
    }
    FocusFrame { anchors.fill: parent; showFocus: root.activeFocus }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: mouseX < root.resizeHandleWidth || mouseX > width - root.resizeHandleWidth ? CursorTokens.resizeHorizontal : CursorTokens.moveItem
        property real originX: 0
        property real originalStart: 0
        property real originalEnd: 0
        property int edge: 0
        property bool moved: false
        onPressed: function(mouse) {
            if (!root.prepareSelection()) return
            root.forceActiveFocus()
            originalStart = root.startMs
            originalEnd = root.endMs
            originX = mapToItem(root.parent, mouse.x, mouse.y).x
            edge = mouse.x < root.resizeHandleWidth ? -1 : mouse.x > width - root.resizeHandleWidth ? 1 : 0
            moved = false
            root.draftStart = originalStart
            root.draftEnd = originalEnd
            root.dragging = true
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !root.dragging || root.pixelsPerMs <= 0
                    || root.upperMs - root.lowerMs < root.minimumDuration) return
            const deltaX = mapToItem(root.parent, mouse.x, mouse.y).x - originX
            if (Math.abs(deltaX) > 3) moved = true
            const delta = Math.round(deltaX / root.pixelsPerMs)
            if (edge < 0) root.draftStart = Math.max(root.lowerMs, Math.min(originalEnd - root.minimumDuration, originalStart + delta))
            else if (edge > 0) root.draftEnd = Math.min(root.upperMs, Math.max(originalStart + root.minimumDuration, originalEnd + delta))
            else {
                const length = originalEnd - originalStart
                root.draftStart = Math.max(root.lowerMs, Math.min(root.upperMs - length, originalStart + delta))
                root.draftEnd = root.draftStart + length
            }
        }
        onReleased: {
            if (!root.dragging) return
            const start = root.draftStart
            const end = root.openEnded && edge !== 1 ? -1 : root.draftEnd
            root.dragging = false
            if (moved && (start !== originalStart || root.draftEnd !== originalEnd)) root.rangeEdited(start, end)
            else root.activated()
        }
        onCanceled: root.dragging = false
    }
}
