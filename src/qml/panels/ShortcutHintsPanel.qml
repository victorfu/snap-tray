import QtQuick
import SnapTrayQml

Item {
    id: root

    required property QtObject viewModel

    property int padding: 14
    property int columnGap: 16
    property int rowGap: 8

    implicitWidth: contentColumn.width + 2 * padding
    implicitHeight: contentColumn.height + 2 * padding

    opacity: 0
    Component.onCompleted: fadeIn.start()

    NumberAnimation on opacity {
        id: fadeIn
        from: 0; to: 1
        duration: SemanticTokens.durationFast
        easing.type: Easing.OutCubic
        running: false
    }

    GlassSurface {
        anchors.fill: parent
        glassBg: ComponentTokens.captureOverlayPanelBg
        glassBgTop: ComponentTokens.captureOverlayPanelBgTop
        glassHighlight: ComponentTokens.captureOverlayPanelHighlight
        glassBorder: ComponentTokens.captureOverlayPanelBorder
        glassRadius: ComponentTokens.captureOverlayPanelRadius
    }

    Column {
        id: contentColumn
        x: root.padding
        y: root.padding
        spacing: root.rowGap

        Repeater {
            model: root.viewModel.hints

            Row {
                spacing: root.columnGap

                // Key group (fixed width for alignment)
                Item {
                    width: keyGroupRow.implicitWidth
                    height: 28
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        id: keyGroupRow
                        spacing: 4

                        Repeater {
                            model: modelData.keys

                            Row {
                                spacing: 4

                                // "+" separator before non-first keys
                                Text {
                                    visible: index > 0
                                    text: "+"
                                    color: Qt.rgba(0.82, 0.82, 0.82, 0.9)
                                    font.pixelSize: PrimitiveTokens.fontSizeSmallBody
                                    font.family: SemanticTokens.fontFamily
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                // Keycap
                                Rectangle {
                                    width: Math.max(30, keycapText.implicitWidth + 18)
                                    height: 28
                                    radius: ComponentTokens.captureOverlayKeycapRadius
                                    color: ComponentTokens.captureOverlayKeycapBg
                                    border.width: 1
                                    border.color: ComponentTokens.captureOverlayKeycapBorder

                                    Text {
                                        id: keycapText
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: ComponentTokens.captureOverlayKeycapText
                                        font.pixelSize: PrimitiveTokens.fontSizeSmallBody
                                        font.weight: Font.Bold
                                        font.family: SemanticTokens.fontFamily
                                    }
                                }
                            }
                        }
                    }
                }

                // Description
                Text {
                    text: modelData.description
                    color: ComponentTokens.captureOverlayHintText
                    font.pixelSize: PrimitiveTokens.fontSizeSmallBody
                    font.family: SemanticTokens.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
