import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * ThumbnailCard: Pin history thumbnail tile with async image loading states.
 */
Item {
    id: root

    property url thumbnailUrl
    property string titleText: ""
    property string subtitleText: ""
    property string tooltipText: ""
    property bool selected: false
    property bool broken: false
    property int imageWidthHint: 180
    property int imageHeightHint: 120

    signal activated()
    signal contextMenuRequested(real globalX, real globalY)

    implicitWidth: 210
    implicitHeight: 165
    width: implicitWidth
    height: implicitHeight

    ToolTip.visible: mouseArea.containsMouse && root.tooltipText !== ""
    ToolTip.text: root.tooltipText

    Rectangle {
        id: card
        anchors.fill: parent
        radius: ComponentTokens.panelRadius
        color: mouseArea.pressed
            ? ComponentTokens.thumbnailCardSelected
            : root.selected
                ? ComponentTokens.thumbnailCardSelected
                : mouseArea.containsMouse
                    ? ComponentTokens.thumbnailCardHover
                    : ComponentTokens.thumbnailCardBackground
        border.width: 1
        border.color: root.selected
            ? ComponentTokens.thumbnailCardSelectedBorder
            : ComponentTokens.thumbnailCardBorder

        Behavior on color {
            ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
        }

        Behavior on border.color {
            ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
        }
    }

    Rectangle {
        id: imageFrame
        anchors.top: parent.top
        anchors.topMargin: SemanticTokens.spacing8
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.imageWidthHint
        height: root.imageHeightHint
        radius: SemanticTokens.radiusMedium
        color: root.broken
            ? ComponentTokens.thumbnailCardErrorBackground
            : ComponentTokens.beautifyPreviewPlaceholder
        border.width: 1
        border.color: ComponentTokens.beautifyPreviewFrame
        clip: true
    }

    Rectangle {
        anchors.fill: imageFrame
        radius: imageFrame.radius
        visible: preview.status === Image.Loading
        color: ComponentTokens.beautifyPreviewPlaceholder
        opacity: 0.8
    }

    Image {
        id: preview
        anchors.fill: imageFrame
        anchors.margins: 1
        source: root.thumbnailUrl
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: false
        sourceSize.width: root.imageWidthHint * 2
        sourceSize.height: root.imageHeightHint * 2
        visible: !root.broken && status === Image.Ready
        smooth: true
    }

    Text {
        visible: root.broken || preview.status === Image.Error
        anchors.centerIn: imageFrame
        width: imageFrame.width - SemanticTokens.spacing16
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("Preview unavailable")
        color: SemanticTokens.textSecondary
        font.pixelSize: SemanticTokens.fontSizeSmall
        font.family: SemanticTokens.fontFamily
    }

    Text {
        anchors.top: imageFrame.bottom
        anchors.topMargin: SemanticTokens.spacing8
        anchors.left: parent.left
        anchors.leftMargin: SemanticTokens.spacing8
        anchors.right: parent.right
        anchors.rightMargin: SemanticTokens.spacing8
        text: root.titleText
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: SemanticTokens.textPrimary
        font.pixelSize: SemanticTokens.fontSizeCaption
        font.family: SemanticTokens.fontFamily
        font.weight: SemanticTokens.fontWeightMedium
    }

    Text {
        anchors.top: imageFrame.bottom
        anchors.topMargin: 26
        anchors.left: parent.left
        anchors.leftMargin: SemanticTokens.spacing8
        anchors.right: parent.right
        anchors.rightMargin: SemanticTokens.spacing8
        text: root.subtitleText
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: SemanticTokens.textSecondary
        font.pixelSize: SemanticTokens.fontSizeSmall
        font.family: SemanticTokens.fontFamily
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: CursorTokens.clickable

        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                var point = root.mapToGlobal(mouse.x, mouse.y)
                root.contextMenuRequested(point.x, point.y)
                return
            }
            root.activated()
        }
    }
}
