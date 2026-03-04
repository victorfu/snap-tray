import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * AdvancedSettings: Capture hints, MCP, blur, pin window options.
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

        SettingsToggle {
            label: qsTr("Show shortcut hints")
            checked: settingsBackend.shortcutHintsEnabled
            onToggled: settingsBackend.shortcutHintsEnabled = checked
        }

        // MCP section (debug builds only)
        SettingsSection {
            title: qsTr("MCP")
            visible: settingsBackend.isMcpBuild
        }

        SettingsToggle {
            label: qsTr("Enable MCP server")
            checked: settingsBackend.isMcpBuild ? settingsBackend.mcpEnabled : false
            visible: settingsBackend.isMcpBuild
            onToggled: {
                if (settingsBackend.isMcpBuild)
                    settingsBackend.mcpEnabled = checked
            }
        }

        SettingsSection { title: qsTr("Blur") }

        SettingsSlider {
            label: qsTr("Blur intensity")
            value: settingsBackend.blurIntensity
            from: 1
            to: 100
            onMoved: settingsBackend.blurIntensity = value
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
            onMoved: settingsBackend.pinDefaultOpacity = value
        }

        SettingsSlider {
            label: qsTr("Opacity step")
            value: settingsBackend.pinOpacityStep
            from: 1
            to: 20
            suffix: "%"
            onMoved: settingsBackend.pinOpacityStep = value
        }

        SettingsSlider {
            label: qsTr("Zoom step")
            value: settingsBackend.pinZoomStep
            from: 1
            to: 20
            suffix: "%"
            onMoved: settingsBackend.pinZoomStep = value
        }

        SettingsSlider {
            label: qsTr("Max cache files")
            value: settingsBackend.pinMaxCacheFiles
            from: 5
            to: 200
            onMoved: settingsBackend.pinMaxCacheFiles = value
        }
    }
}
