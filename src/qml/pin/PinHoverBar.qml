import QtQuick
import SnapTrayQml

// The pin hover bar (#225): zoom and opacity values, the Live status chip,
// Annotate and More. Values open PinValuePanel; PinHoverBar (C++) places it.
GlassSurface {
    id: root
    required property var viewModel

    implicitWidth: row.implicitWidth + 2 * SemanticTokens.spacing4
    implicitHeight: 32
    width: implicitWidth
    height: implicitHeight

    // The width of the full (not compact) bar, for PinHoverBar to decide
    // whether it fits the pin.
    readonly property real fullWidth: 2 * SemanticTokens.spacing4
        + zoomButton.implicitWidth + opacityButton.implicitWidth
        + (root.viewModel.status !== 0 ? statusButton.implicitWidth + row.spacing : 0)
        + annotateButton.width + moreButton.width + 3 * row.spacing
    // The compact bar: the two values and More. (Bindings, so both are known
    // before the Row re-lays out.)
    readonly property real compactWidth: 2 * SemanticTokens.spacing4
        + zoomButton.implicitWidth + opacityButton.implicitWidth + moreButton.width + 2 * row.spacing

    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Pin controls")

    // Window coordinates of a named button, for anchoring the value panel.
    function buttonRect(name) {
        const item = buttonFor(name)
        if (!item || !item.visible) return Qt.rect(0, 0, 0, 0)
        const p = item.mapToItem(root, 0, 0)
        return Qt.rect(p.x, p.y, item.width, item.height)
    }
    function focusButton(name) {
        const item = buttonFor(name)
        if (item) item.forceActiveFocus(Qt.TabFocusReason)
    }
    function buttonFor(name) {
        return ({ "pinHoverZoom": zoomButton, "pinHoverOpacity": opacityButton,
                  "pinHoverStatus": statusButton, "pinHoverAnnotate": annotateButton,
                  "pinHoverMore": moreButton })[name] || null
    }

    component ValueButton: Rectangle {
        id: button
        property string label: ""
        property bool active: false
        signal activated()
        implicitWidth: labelText.implicitWidth + 2 * SemanticTokens.spacing8
        implicitHeight: 24
        radius: 4
        color: area.containsMouse || button.activeFocus || button.active
            ? (SemanticTokens.isDarkMode ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.08))
            : "transparent"
        border.width: button.activeFocus ? 1 : 0
        border.color: DesignSystem.accentDefault
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: label
        Accessible.onPressAction: button.activated()
        Keys.onReturnPressed: button.activated()
        Keys.onEnterPressed: button.activated()
        Keys.onSpacePressed: button.activated()
        Text {
            id: labelText
            anchors.centerIn: parent
            text: button.label
            font.pixelSize: SemanticTokens.fontSizeSmall
            font.family: SemanticTokens.fontFamily
            color: ComponentTokens.toolbarIcon
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: CursorTokens.toolbarControl
            onClicked: button.activated()
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        ValueButton {
            id: zoomButton
            objectName: "pinHoverZoom"
            label: qsTr("Zoom %1%").arg(root.viewModel.zoomPercent)
            active: root.viewModel.panel === 1
            anchors.verticalCenter: parent.verticalCenter
            KeyNavigation.right: opacityButton
            onActivated: root.viewModel.openPanel(1)
        }
        ValueButton {
            id: opacityButton
            objectName: "pinHoverOpacity"
            label: qsTr("Opacity %1%").arg(root.viewModel.opacityPercent)
            active: root.viewModel.panel === 2
            anchors.verticalCenter: parent.verticalCenter
            KeyNavigation.left: zoomButton
            KeyNavigation.right: statusButton.visible ? statusButton
                : (annotateButton.visible ? annotateButton : moreButton)
            onActivated: root.viewModel.openPanel(2)
        }
        ValueButton {
            id: statusButton
            objectName: "pinHoverStatus"
            visible: !root.viewModel.compact && root.viewModel.status !== 0
            label: root.viewModel.status === 2 ? qsTr("⏸ Paused") : qsTr("● Live")
            Accessible.name: root.viewModel.status === 2 ? qsTr("Resume Live Update (P)")
                                                          : qsTr("Pause Live Update (P)")
            anchors.verticalCenter: parent.verticalCenter
            KeyNavigation.left: opacityButton
            KeyNavigation.right: annotateButton.visible ? annotateButton : moreButton
            onActivated: root.viewModel.toggleStatus()
        }
        ToolbarButton {
            id: annotateButton
            objectName: "pinHoverAnnotate"
            visible: !root.viewModel.compact
            iconSource: "qrc:/icons/icons/pencil.svg"
            tooltipText: qsTr("Annotate (Space)")
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Annotate")
            anchors.verticalCenter: parent.verticalCenter
            Keys.onReturnPressed: root.viewModel.requestAnnotate()
            Keys.onSpacePressed: root.viewModel.requestAnnotate()
            onClicked: root.viewModel.requestAnnotate()
        }
        ToolbarButton {
            id: moreButton
            objectName: "pinHoverMore"
            textLabel: "⋯"
            tooltipText: qsTr("More")
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("More")
            anchors.verticalCenter: parent.verticalCenter
            Keys.onReturnPressed: root.viewModel.requestMore()
            Keys.onSpacePressed: root.viewModel.requestMore()
            onClicked: root.viewModel.requestMore()
        }
    }
}
