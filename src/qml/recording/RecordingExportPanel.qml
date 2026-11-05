import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SnapTrayQml

Popup {
    id: root
    objectName: "previewExportPanel"
    property alias formatContent: formats
    property alias optionsContent: options
    // What the export will contain (sizes, kept time, limits), under the formats.
    property string summaryText: ""
    // Next to the primary action, e.g. the MP4 size estimate.
    property string footerText: ""
    // The current format has its own options (set by the owner; a hidden
    // column measures zero, so its size cannot decide this).
    property bool showOptions: true
    property var returnFocusItem: null
    signal exportRequested()
    signal returnRequested()
    anchors.centerIn: parent
    width: Math.min(460, parent.width - 32)
    // As tall as its content: a format without options has no empty area.
    height: Math.min(implicitContentHeight + topPadding + bottomPadding, parent.height - 32)
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: 20
    background: Rectangle {
        color: ComponentTokens.editorSurface
        radius: SemanticTokens.radiusMedium
        border.color: ComponentTokens.editorBorder
    }
    Overlay.modal: Rectangle { color: ComponentTokens.editorScrim }
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
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Export recording")
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeH3
                font.weight: SemanticTokens.fontWeightSemiBold
            }
            Rectangle {
                id: closeButton
                objectName: "previewExportClose"
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: SemanticTokens.radiusSmall
                color: closeMouse.containsMouse ? ComponentTokens.editorControlHover : "transparent"
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Back to editing")
                Accessible.onPressAction: root.returnRequested()
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) root.returnRequested() }
                Keys.onSpacePressed: function(event) { if (!event.isAutoRepeat) root.returnRequested() }
                FocusFrame { anchors.fill: parent; showFocus: closeButton.activeFocus }
                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/close.svg"
                    iconSize: 16
                    color: SemanticTokens.textSecondary
                }
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: CursorTokens.clickable
                    onClicked: root.returnRequested()
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Create a video or image file. Your edits stay in this recording.")
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeSmall
            wrapMode: Text.WordWrap
        }
        Item { id: formats; Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height }
        Text {
            objectName: "previewOutputSummary"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.summaryText
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeCaption
            wrapMode: Text.WordWrap
        }
        ScrollView {
            id: contentScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: options.childrenRect.height
            Layout.minimumHeight: Math.min(options.childrenRect.height, 96)
            visible: root.showOptions
            contentWidth: availableWidth
            contentHeight: options.childrenRect.height
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Item { id: options; width: contentScroll.availableWidth }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: ComponentTokens.editorBorder
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            DialogButton {
                objectName: "previewExportBack"
                style: "ghost"
                text: qsTr("Back to editing")
                onClicked: root.returnRequested()
            }
            Text {
                objectName: "previewExportFooter"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: root.footerText
                color: SemanticTokens.textSecondary
                font.pixelSize: SemanticTokens.fontSizeCaption
                wrapMode: Text.WordWrap
            }
            DialogButton {
                objectName: "previewSaveButton"
                text: qsTr("Export…")
                style: "primary"
                onClicked: root.exportRequested()
            }
        }
    }
}
