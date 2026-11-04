import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

// Stable visual slots: the player and inspector are never recreated on navigation.
Item {
    id: root
    property bool inspectorVisible: true
    property bool inspectorOverlay: false
    property string inspectorTitle: ""
    property alias workspace: workspace
    property alias headerContent: headerContent
    property alias inspectorContent: inspectorContent
    readonly property int headerHeight: 52
    readonly property int inspectorWidth: Math.min(304, width - 32)
    signal projectRequested()
    signal inspectorCloseRequested()

    Rectangle {
        anchors.fill: parent
        color: ComponentTokens.editorSurface
    }
    Rectangle {
        width: parent.width
        height: root.headerHeight
        color: ComponentTokens.editorSurface
        Item {
            id: headerContent
            anchors.fill: parent
            anchors.margins: SemanticTokens.spacing12
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: ComponentTokens.editorBorder }
    }
    Item {
        id: workspace
        objectName: "editorWorkspaceSlot"
        y: root.headerHeight
        width: parent.width - (root.inspectorVisible && !root.inspectorOverlay ? root.inspectorWidth : 0)
        height: parent.height - y
    }
    Rectangle {
        id: inspector
        objectName: "editorInspector"
        visible: root.inspectorVisible
        anchors.right: parent.right
        y: root.headerHeight
        width: root.inspectorWidth
        height: parent.height - y - (root.inspectorOverlay ? 68 : 0)
        color: ComponentTokens.editorSurface
        border.color: ComponentTokens.editorBorder
        z: 10
        // An overlay inspector blocks clicks through to the canvas.
        MouseArea { anchors.fill: parent }
        Column {
            id: inspectorHeader
            x: 12; y: 12
            width: parent.width - 24
            spacing: 8
            Text {
                width: parent.width
                text: root.inspectorTitle
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeBody
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Flow {
                width: parent.width
                spacing: 8
                DialogButton {
                    text: qsTr("Project settings")
                    onClicked: root.projectRequested()
                }
                DialogButton {
                    visible: root.inspectorOverlay
                    text: qsTr("Hide")
                    onClicked: root.inspectorCloseRequested()
                }
            }
        }
        ScrollView {
            id: contentScroll
            x: 0
            y: inspectorHeader.y + inspectorHeader.height + 12
            width: parent.width
            height: parent.height - y
            clip: true
            contentWidth: availableWidth
            contentHeight: inspectorContent.childrenRect.height
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Item { id: inspectorContent; width: contentScroll.availableWidth }
        }
    }
}
