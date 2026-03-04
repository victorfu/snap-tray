import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * RecordingSettings: Frame rate, output format, quality, audio, preview, countdown.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    readonly property var frameRates: [10, 15, 24, 30]

    function frameRateToIndex(fps) {
        for (var i = 0; i < frameRates.length; i++) {
            if (frameRates[i] === fps) return i
        }
        return 3 // default to 30
    }

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: 4

        SettingsCombo {
            label: qsTr("Frame rate")
            model: [
                { text: qsTr("10 FPS"), value: 10 },
                { text: qsTr("15 FPS"), value: 15 },
                { text: qsTr("24 FPS"), value: 24 },
                { text: qsTr("30 FPS"), value: 30 }
            ]
            currentIndex: root.frameRateToIndex(settingsBackend.recordingFrameRate)
            onActivated: function(index) {
                settingsBackend.recordingFrameRate = root.frameRates[index]
            }
        }

        SettingsCombo {
            label: qsTr("Output format")
            model: [
                { text: qsTr("MP4 (H.264)"), value: 0 },
                { text: qsTr("GIF"), value: 1 },
                { text: qsTr("WebP"), value: 2 }
            ]
            currentIndex: settingsBackend.recordingOutputFormat
            onActivated: function(index) { settingsBackend.recordingOutputFormat = index }
        }

        // MP4 quality slider (visible when format == 0)
        SettingsSlider {
            label: qsTr("Quality")
            value: settingsBackend.recordingQuality
            from: 0
            to: 100
            visible: settingsBackend.recordingOutputFormat === 0
            onMoved: settingsBackend.recordingQuality = value
        }

        // GIF / WebP info panel
        Rectangle {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: formatInfoText.implicitHeight + 20
            radius: PrimitiveTokens.radiusSmall
            color: ComponentTokens.infoPanelBg
            border.width: 1
            border.color: ComponentTokens.infoPanelBorder
            visible: settingsBackend.recordingOutputFormat > 0

            Text {
                id: formatInfoText
                anchors.fill: parent
                anchors.margins: 10
                text: settingsBackend.recordingOutputFormat === 2
                    ? qsTr("WebP format creates smaller files than GIF with better quality.\nBest for short clips and sharing on web.\nAudio is not supported for WebP recordings.")
                    : qsTr("GIF format creates larger files than MP4.\nBest for short clips and sharing on web.\nAudio is not supported for GIF recordings.")
                color: ComponentTokens.infoPanelText
                font.pixelSize: PrimitiveTokens.fontSizeBody
                font.family: PrimitiveTokens.fontFamily
                font.letterSpacing: -0.2
                wrapMode: Text.WordWrap
            }
        }

        SettingsSection { title: qsTr("Audio") }

        SettingsToggle {
            label: qsTr("Record audio")
            checked: settingsBackend.recordingAudioEnabled
            onToggled: settingsBackend.recordingAudioEnabled = checked
        }

        SettingsCombo {
            label: qsTr("Source")
            model: [
                { text: qsTr("Microphone"), value: 0 },
                { text: qsTr("System Audio"), value: 1 },
                { text: qsTr("Both (Mixed)"), value: 2 }
            ]
            currentIndex: settingsBackend.recordingAudioSource
            enabled: settingsBackend.recordingAudioEnabled
                && settingsBackend.recordingOutputFormat === 0
            onActivated: function(index) { settingsBackend.recordingAudioSource = index }
        }

        Item { width: 1; height: 8 }

        SettingsToggle {
            label: qsTr("Show preview")
            checked: settingsBackend.recordingShowPreview
            onToggled: settingsBackend.recordingShowPreview = checked
        }

        SettingsSection { title: qsTr("Countdown") }

        SettingsToggle {
            label: qsTr("Show countdown")
            checked: settingsBackend.countdownEnabled
            onToggled: settingsBackend.countdownEnabled = checked
        }

        SettingsCombo {
            label: qsTr("Duration")
            model: [
                { text: qsTr("1 second"), value: 1 },
                { text: qsTr("2 seconds"), value: 2 },
                { text: qsTr("3 seconds"), value: 3 },
                { text: qsTr("4 seconds"), value: 4 },
                { text: qsTr("5 seconds"), value: 5 }
            ]
            currentIndex: settingsBackend.countdownSeconds - 1
            enabled: settingsBackend.countdownEnabled
            onActivated: function(index) { settingsBackend.countdownSeconds = index + 1 }
        }
    }
}
