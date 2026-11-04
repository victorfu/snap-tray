import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SnapTrayQml

Popup {
    id: root
    objectName: "previewExportPanel"
    property alias formatContent: formats
    property alias optionsContent: options
    property bool detailedOptions: true
    property var returnFocusItem: null
    signal exportRequested()
    signal returnRequested()
    anchors.centerIn: parent
    width: Math.min(440, parent.width - 32)
    height: Math.min(detailedOptions ? 620 : 280, parent.height - 32)
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: 20
    background: Rectangle {
        color: ComponentTokens.editorSurface
        radius: SemanticTokens.radiusMedium
        border.color: ComponentTokens.editorBorder
    }
    onClosed: {
        if (returnFocusItem && returnFocusItem.visible && returnFocusItem.enabled)
            returnFocusItem.forceActiveFocus()
        else if (parent) parent.forceActiveFocus()
        returnFocusItem = null
    }
    contentItem: ColumnLayout {
        focus: true
        Keys.onEscapePressed: root.returnRequested()
        spacing: 12
        Text { text: qsTr("Export recording"); color: SemanticTokens.textPrimary; font.pixelSize: SemanticTokens.fontSizeH3 }
        Text {
            Layout.fillWidth: true
            text: qsTr("Create a video or image file. Editing settings remain in this recording.")
            color: SemanticTokens.textSecondary
            wrapMode: Text.WordWrap
        }
        Item { id: formats; Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height }
        ScrollView {
            id: contentScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            contentHeight: options.childrenRect.height
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Item { id: options; width: contentScroll.availableWidth }
        }
        RowLayout {
            Layout.fillWidth: true
            DialogButton { objectName: "previewExportBack"; text: qsTr("Back to editing"); onClicked: root.returnRequested() }
            Item { Layout.fillWidth: true }
            DialogButton {
                objectName: "previewSaveButton"
                text: qsTr("Export…")
                style: "primary"
                onClicked: root.exportRequested()
            }
        }
    }
}
