import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

// The value panel of the pin hover bar (#225): a slider, presets and a reset
// for zoom (panel 1) or opacity (panel 2).
GlassSurface {
    id: root
    required property var viewModel

    readonly property bool isZoom: root.viewModel.panel === 1
    readonly property int minimum: isZoom ? root.viewModel.minZoomPercent : 10
    readonly property int maximum: isZoom ? 500 : 100
    readonly property int current: isZoom ? root.viewModel.zoomPercent : root.viewModel.opacityPercent
    readonly property var presets: isZoom ? [33, 50, 100, 200] : [50, 75, 100]

    implicitWidth: 240
    implicitHeight: column.implicitHeight + 2 * SemanticTokens.spacing12
    width: implicitWidth
    height: implicitHeight
    visible: root.viewModel.panel !== 0

    Accessible.role: Accessible.Dialog
    Accessible.name: isZoom ? qsTr("Zoom") : qsTr("Opacity")

    // Flat buttons in the panel's own text colour (the control style's
    // default text is too faint on the glass surface).
    component PanelButton: Button {
        id: control
        flat: true
        font.pixelSize: SemanticTokens.fontSizeSmall
        font.family: SemanticTokens.fontFamily
        contentItem: Text {
            text: control.text
            font: control.font
            color: ComponentTokens.toolbarIcon
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 4
            color: control.hovered || control.visualFocus
                ? (SemanticTokens.isDarkMode ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.08))
                : "transparent"
            border.width: control.visualFocus ? 1 : 0
            border.color: DesignSystem.accentDefault
        }
    }

    function request(percent) {
        if (isZoom) root.viewModel.requestZoomPercent(percent)
        else root.viewModel.requestOpacityPercent(percent)
    }
    function focusSlider() { slider.forceActiveFocus(Qt.TabFocusReason) }

    Column {
        id: column
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacing12
        spacing: SemanticTokens.spacing8

        Row {
            width: parent.width
            Text {
                objectName: "pinValueTitle"
                text: root.isZoom ? qsTr("Zoom") : qsTr("Opacity")
                width: parent.width - valueText.width
                font.pixelSize: SemanticTokens.fontSizeSmall
                font.family: SemanticTokens.fontFamily
                font.bold: true
                color: ComponentTokens.toolbarIcon
            }
            Text {
                id: valueText
                text: qsTr("%1%").arg(root.current)
                font.pixelSize: SemanticTokens.fontSizeSmall
                font.family: SemanticTokens.fontFamily
                color: ComponentTokens.toolbarIcon
            }
        }

        Slider {
            id: slider
            objectName: "pinValueSlider"
            width: parent.width
            from: root.minimum
            to: root.maximum
            stepSize: 1
            value: root.current
            Accessible.name: root.isZoom ? qsTr("Zoom level") : qsTr("Opacity level")
            onMoved: root.request(Math.round(value))
        }

        Row {
            id: presetRow
            width: parent.width
            spacing: SemanticTokens.spacing4
            Repeater {
                model: root.presets
                delegate: PanelButton {
                    required property int modelData
                    // The presets share the panel width, so none falls outside it.
                    width: (presetRow.width - presetRow.spacing * (root.presets.length - 1)) / root.presets.length
                    leftPadding: 0
                    rightPadding: 0
                    objectName: "pinValuePreset_" + modelData
                    text: qsTr("%1%").arg(modelData)
                    Accessible.name: (root.isZoom ? qsTr("Zoom to %1%") : qsTr("Opacity %1%")).arg(modelData)
                    onClicked: root.request(modelData)
                }
            }
        }

        PanelButton {
            objectName: "pinValueReset"
            text: qsTr("Reset to 100%")
            Accessible.name: root.isZoom ? qsTr("Reset zoom to 100%") : qsTr("Reset opacity to 100%")
            onClicked: root.request(100)
        }
    }
}
