import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SnapTrayQml

Rectangle {
    id: root
    property string script
    property int fontPixels: 28
    color: SemanticTokens.backgroundPrimary

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        RowLayout {
            Text {
                text: qsTr("Teleprompter")
                color: SemanticTokens.textPrimary
                Layout.fillWidth: true
                font.family: SemanticTokens.fontFamily
            }
            DialogButton {
                text: "A−"
                Accessible.name: qsTr("Decrease text size")
                onClicked: root.fontPixels = Math.max(14, root.fontPixels - 2)
            }
            DialogButton {
                text: "A+"
                Accessible.name: qsTr("Increase text size")
                onClicked: root.fontPixels = Math.min(64, root.fontPixels + 2)
            }
        }
        Flickable {
            id: reader
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: words.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Text {
                id: words
                width: reader.width - 14
                text: root.script
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: root.fontPixels
                font.family: SemanticTokens.fontFamily
                color: SemanticTokens.textPrimary
            }
        }
    }
}
