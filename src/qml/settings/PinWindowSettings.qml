import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * PinWindowSettings (#227): how new pins look and how far each zoom or opacity step goes.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: ComponentTokens.settingsColumnSpacing

        SettingsPageHeader {
            title: qsTr("Pin Windows")
            description: qsTr("How new pins look, and how much each zoom or opacity step changes.")
        }

        SettingsSection { title: qsTr("Appearance") }

        SettingsSlider {
            resetKey: "pinDefaultOpacity"
            objectName: "pinDefaultOpacitySlider"
            label: qsTr("Default opacity")
            value: settingsBackend.pinDefaultOpacity
            from: 10
            to: 100
            suffix: "%"
            onMoved: function(value) { settingsBackend.pinDefaultOpacity = value }
        }

        SettingsToggle {
            resetKey: "pinShowHoverControls"
            objectName: "pinHoverControlsToggle"
            label: qsTr("Show controls on hover")
            checked: settingsBackend.pinShowHoverControls
            onToggled: function(checked) { settingsBackend.pinShowHoverControls = checked }
        }

        SettingsSection { title: qsTr("Steps") }

        SettingsSlider {
            resetKey: "pinOpacityStep"
            objectName: "pinOpacityStepSlider"
            label: qsTr("Opacity step")
            value: settingsBackend.pinOpacityStep
            from: 1
            to: 20
            suffix: "%"
            onMoved: function(value) { settingsBackend.pinOpacityStep = value }
        }

        SettingsSlider {
            resetKey: "pinZoomStep"
            objectName: "pinZoomStepSlider"
            label: qsTr("Zoom step")
            value: settingsBackend.pinZoomStep
            from: 1
            to: 20
            suffix: "%"
            onMoved: function(value) { settingsBackend.pinZoomStep = value }
        }
    }
}
