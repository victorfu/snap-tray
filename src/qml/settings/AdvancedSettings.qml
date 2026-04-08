import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * AdvancedSettings: Capture hints, blur, and pin window options.
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

        SettingsSection { title: qsTr("Capture") }

        SettingsCombo {
            label: qsTr("Cursor companion")
            model: [
                { text: qsTr("Off"), value: 0 },
                { text: qsTr("Magnifier"), value: 1 },
                { text: qsTr("Beaver"), value: 2 }
            ]
            currentIndex: settingsBackend.cursorCompanionStyle
            onActivated: function(index) { settingsBackend.cursorCompanionStyle = index }
        }

        SettingsToggle {
            label: qsTr("Show shortcut hints")
            checked: settingsBackend.shortcutHintsEnabled
            onToggled: function(checked) { settingsBackend.shortcutHintsEnabled = checked }
        }

        SettingsSection { title: qsTr("Blur") }

        SettingsSlider {
            label: qsTr("Blur intensity")
            value: settingsBackend.blurIntensity
            from: 1
            to: 100
            onMoved: function(value) { settingsBackend.blurIntensity = value }
        }

        SettingsCombo {
            label: qsTr("Blur type")
            model: [
                { text: qsTr("Pixelate"), value: 0 },
                { text: qsTr("Gaussian"), value: 1 }
            ]
            currentIndex: settingsBackend.blurType
            onActivated: function(index) { settingsBackend.blurType = index }
        }

        SettingsSection { title: qsTr("Pin Window") }

        SettingsSlider {
            label: qsTr("Default opacity")
            value: settingsBackend.pinDefaultOpacity
            from: 10
            to: 100
            suffix: "%"
            onMoved: function(value) { settingsBackend.pinDefaultOpacity = value }
        }

        SettingsSlider {
            label: qsTr("Opacity step")
            value: settingsBackend.pinOpacityStep
            from: 1
            to: 20
            suffix: "%"
            onMoved: function(value) { settingsBackend.pinOpacityStep = value }
        }

        SettingsSlider {
            label: qsTr("Zoom step")
            value: settingsBackend.pinZoomStep
            from: 1
            to: 20
            suffix: "%"
            onMoved: function(value) { settingsBackend.pinZoomStep = value }
        }

        SettingsSlider {
            label: qsTr("Max history entries")
            value: settingsBackend.pinMaxCacheFiles
            from: 5
            to: 200
            onMoved: function(value) { settingsBackend.pinMaxCacheFiles = value }
        }
    }
}
