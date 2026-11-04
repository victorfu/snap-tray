import QtQuick
import SnapTrayQml

// A draft is local until release: one backend mutation and one undo step per drag.
Rectangle {
    id: root
    property real startMs: 0
    property real endMs: 0
    property real lowerMs: 0
    property real upperMs: 1
    property real minimumDuration: 100
    property real pixelsPerMs: 1
    property real trackX: 0
    property bool selected: false
    property string label: ""
    property var prepareSelection: function() { return true }
    signal activated()
    signal rangeEdited(real start, real end)
    property bool dragging: false
    property real draftStart: startMs
    property real draftEnd: endMs
    x: trackX + (dragging ? draftStart : startMs) * pixelsPerMs
    width: Math.max(8, ((dragging ? draftEnd : endMs) - (dragging ? draftStart : startMs)) * pixelsPerMs)
    height: 24
    radius: 4
    color: selected ? ComponentTokens.editorSelection : ComponentTokens.editorSurface
    border.color: selected ? SemanticTokens.accentDefault : ComponentTokens.editorBorder
    border.width: selected ? 2 : 1
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.onPressAction: if (prepareSelection()) activated()
    Keys.onReturnPressed: if (prepareSelection()) activated()
    Keys.onSpacePressed: if (prepareSelection()) activated()
    Keys.onEscapePressed: function(event) {
        if (dragging) dragging = false
        else event.accepted = false
    }
    Text {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        text: root.label
        color: root.selected ? SemanticTokens.accentDefault : SemanticTokens.textPrimary
        font.pixelSize: SemanticTokens.fontSizeCaption
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    FocusFrame { anchors.fill: parent; showFocus: root.activeFocus }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: mouseX < 6 || mouseX > width - 6 ? CursorTokens.resizeHorizontal : CursorTokens.moveItem
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
            edge = mouse.x < 6 ? -1 : mouse.x > width - 6 ? 1 : 0
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
            const end = root.draftEnd
            root.dragging = false
            if (moved && (start !== originalStart || end !== originalEnd)) root.rangeEdited(start, end)
            else root.activated()
        }
        onCanceled: root.dragging = false
    }
}
