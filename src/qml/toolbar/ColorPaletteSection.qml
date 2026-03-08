import QtQuick
import SnapTrayQml

/**
 * ColorPaletteSection: Custom color swatch + preset color palette.
 *
 * Layout: [Custom swatch] [3px] [6 preset swatches with 3px spacing]
 */
Item {
    id: root
    required property var viewModel

    readonly property int sectionPadding: 4
    readonly property int swatchSize: 20
    readonly property int swatchSpacing: 3
    readonly property int indicatorSize: 10

    implicitWidth: sectionPadding + customSwatch.width + swatchSpacing + paletteRow.width + sectionPadding
    implicitHeight: swatchSize
    width: implicitWidth
    height: implicitHeight

    // Custom color swatch (shows current color)
    Rectangle {
        id: customSwatch
        x: root.sectionPadding
        width: root.swatchSize
        height: root.swatchSize
        radius: 3
        color: root.viewModel.currentColor
        border.width: 1
        border.color: {
            var lum = root.viewModel.currentColor.r * 0.299 +
                      root.viewModel.currentColor.g * 0.587 +
                      root.viewModel.currentColor.b * 0.114
            return lum > 0.7
                ? Qt.rgba(96 / 255, 96 / 255, 96 / 255, 1.0)
                : Qt.rgba(192 / 255, 192 / 255, 192 / 255, 1.0)
        }

        Canvas {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 1
            width: root.indicatorSize
            height: root.indicatorSize

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()

                var cx = width / 2
                var cy = height / 2
                var radius = Math.max(0, Math.min(width, height) / 2 - 1)
                var colors = [
                    "#FF0000",
                    "#FFF200",
                    "#00FF00",
                    "#00FFFF",
                    "#0000FF",
                    "#FF00FF"
                ]

                for (var i = 0; i < colors.length; i++) {
                    var start = (-90 + i * 60) * Math.PI / 180
                    var end = start + (60 * Math.PI / 180)
                    ctx.beginPath()
                    ctx.moveTo(cx, cy)
                    ctx.arc(cx, cy, radius, start, end, false)
                    ctx.closePath()
                    ctx.fillStyle = colors[i]
                    ctx.fill()
                }

                ctx.beginPath()
                ctx.arc(cx, cy, radius / 3, 0, Math.PI * 2, false)
                ctx.fillStyle = "#FFFFFF"
                ctx.fill()
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.ArrowCursor
            hoverEnabled: true
            onClicked: root.viewModel.handleCustomColorClicked()
        }
    }

    // Preset color swatches
    Row {
        id: paletteRow
        anchors.left: customSwatch.right
        anchors.leftMargin: root.swatchSpacing
        spacing: root.swatchSpacing

        Repeater {
            model: root.viewModel.colorPalette

            Rectangle {
                width: root.swatchSize
                height: root.swatchSize
                radius: 3
                color: modelData
                border.width: 1
                border.color: {
                    var lum = modelData.r * 0.299 +
                              modelData.g * 0.587 +
                              modelData.b * 0.114
                    return lum > 0.7
                        ? Qt.rgba(96 / 255, 96 / 255, 96 / 255, 1.0)
                        : Qt.rgba(192 / 255, 192 / 255, 192 / 255, 1.0)
                }

                Rectangle {
                    x: -2
                    y: -2
                    width: parent.width + 4
                    height: parent.height + 4
                    radius: 4
                    color: "transparent"
                    border.width: 2
                    border.color: DesignSystem.accentDefault
                    visible: modelData == root.viewModel.currentColor
                }

                // Hover effect
                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    color: "transparent"
                    border.width: 2
                    border.color: DesignSystem.accentDefault
                    visible: swatchMouse.containsMouse && modelData != root.viewModel.currentColor
                }

                MouseArea {
                    id: swatchMouse
                    anchors.fill: parent
                    cursorShape: Qt.ArrowCursor
                    hoverEnabled: true
                    onClicked: root.viewModel.handleColorClicked(index)
                }
            }
        }
    }
}
