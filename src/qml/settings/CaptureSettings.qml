import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * CaptureSettings (#227): what follows a capture, region selector helpers, Auto Blur.
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
            title: qsTr("Capture")
            description: qsTr("What happens when you take a screenshot, and how the region selector helps you aim.")
        }

        SettingsSection { title: qsTr("After Capture") }

        SettingsCombo {
            resetKey: "fullScreenCaptureAction"
            objectName: "fullScreenCaptureActionCombo"
            label: qsTr("After full screen capture")
            model: [
                { text: qsTr("Copy to clipboard"), value: 0 },
                { text: qsTr("Save to file"), value: 1 },
                { text: qsTr("Open editor"), value: 2 }
            ]
            currentIndex: settingsBackend.fullScreenCaptureAction
            onActivated: function(index) { settingsBackend.fullScreenCaptureAction = index }
        }

        SettingsSection { title: qsTr("Region Selector") }

        SettingsCombo {
            resetKey: "cursorCompanionStyle"
            objectName: "cursorCompanionCombo"
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
            resetKey: "shortcutHintsEnabled"
            objectName: "shortcutHintsToggle"
            label: qsTr("Show shortcut hints")
            checked: settingsBackend.shortcutHintsEnabled
            onToggled: function(checked) { settingsBackend.shortcutHintsEnabled = checked }
        }

        SettingsSection {
            title: qsTr("Auto Blur")
            description: qsTr("How faces and text are hidden when you use Auto Blur.")
        }

        SettingsCombo {
            resetKey: "blurType"
            objectName: "blurTypeCombo"
            label: qsTr("Blur type")
            model: [
                { text: qsTr("Pixelate"), value: 0 },
                { text: qsTr("Gaussian"), value: 1 }
            ]
            currentIndex: settingsBackend.blurType
            onActivated: function(index) { settingsBackend.blurType = index }
        }

        SettingsSlider {
            resetKey: "blurIntensity"
            objectName: "blurIntensitySlider"
            label: qsTr("Blur intensity")
            value: settingsBackend.blurIntensity
            from: 1
            to: 100
            onMoved: function(value) { settingsBackend.blurIntensity = value }
        }
    }
}
