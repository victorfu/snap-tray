import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * HotkeySettings: Grouped hotkey list with edit / clear / reset actions.
 *
 * Uses proportional column widths so Action, Hotkey, and Status columns align
 * across all rows and adapt to different panel widths and languages.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    // Revision counter — incrementing forces inner Repeaters to re-query
    property int hotkeyRevision: 0

    Connections {
        target: settingsBackend
        function onHotkeysChanged() { root.hotkeyRevision++ }
    }

    // Proportional column layout constants
    readonly property int rowLeftMargin: 8
    readonly property int buttonAreaWidth: 210
    readonly property real colActionRatio: 0.50
    readonly property real colKeyRatio: 0.28
    readonly property real colStatusRatio: 0.22
    readonly property int textAreaWidth: width - 2 * ComponentTokens.settingsContentPadding - buttonAreaWidth - rowLeftMargin

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: ComponentTokens.settingsColumnSpacing

        // Header row with "Restore All Defaults" button
        Item {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: 36

            Text {
                text: qsTr("Keyboard Shortcuts")
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeH3
                font.weight: Font.DemiBold
                font.family: SemanticTokens.fontFamily
                font.letterSpacing: SemanticTokens.letterSpacingDefault
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
            }

            SettingsButton {
                id: restoreBtn
                text: qsTr("Restore All Defaults")
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                onClicked: settingsBackend.restoreAllHotkeys()
            }
        }

        Spacer {}

        // Category repeater
        Repeater {
            model: settingsBackend.hotkeyCategories()

            Column {
                width: content.width - 2 * ComponentTokens.settingsContentPadding
                spacing: 2

                required property var modelData

                // Category header
                Text {
                    text: modelData.name.toUpperCase()
                    color: ComponentTokens.settingsSectionText
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.weight: Font.Medium
                    font.family: SemanticTokens.fontFamily
                    font.letterSpacing: SemanticTokens.letterSpacingWide
                    topPadding: 12
                    bottomPadding: 4
                }

                // Hotkey entries (re-queried when hotkeyRevision changes)
                Repeater {
                    model: {
                        void(root.hotkeyRevision)
                        return settingsBackend.hotkeysForCategory(modelData.category)
                    }

                    Rectangle {
                        width: parent.width
                        height: 40
                        radius: SemanticTokens.radiusSmall
                        color: entryHover.containsMouse
                            ? ComponentTokens.settingsSidebarHoverItem
                            : "transparent"

                        required property var modelData

                        MouseArea {
                            id: entryHover
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }

                        // Action name — proportional column
                        Text {
                            id: actionName
                            x: root.rowLeftMargin
                            width: root.textAreaWidth * root.colActionRatio
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            color: SemanticTokens.textPrimary
                            font.pixelSize: SemanticTokens.fontSizeBody
                            font.family: SemanticTokens.fontFamily
                            font.letterSpacing: SemanticTokens.letterSpacingDefault
                            elide: Text.ElideRight

                            ToolTip.visible: actionNameHover.containsMouse && actionName.truncated
                            ToolTip.delay: 500
                            ToolTip.text: modelData.name

                            MouseArea {
                                id: actionNameHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }

                        // Key sequence — proportional column
                        Text {
                            id: keySeq
                            x: root.rowLeftMargin + root.textAreaWidth * root.colActionRatio
                            width: root.textAreaWidth * root.colKeyRatio
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.keySequence || qsTr("Not Set")
                            color: modelData.keySequence
                                ? SemanticTokens.textPrimary
                                : SemanticTokens.textTertiary
                            font.pixelSize: SemanticTokens.fontSizeBody
                            font.family: SemanticTokens.fontFamily
                            font.letterSpacing: SemanticTokens.letterSpacingDefault
                            elide: Text.ElideRight

                            ToolTip.visible: keySeqHover.containsMouse && keySeq.truncated
                            ToolTip.delay: 500
                            ToolTip.text: modelData.keySequence || qsTr("Not Set")

                            MouseArea {
                                id: keySeqHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }

                        // Status text — proportional column
                        // HotkeyStatus enum: Unset=0, Registered=1, Failed=2, Disabled=3
                        Text {
                            id: statusText
                            x: root.rowLeftMargin + root.textAreaWidth * (root.colActionRatio + root.colKeyRatio)
                            width: root.textAreaWidth * root.colStatusRatio
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                if (modelData.status === 2)
                                    return qsTr("Conflict")
                                if (modelData.status === 1)
                                    return qsTr("Active")
                                if (modelData.status === 3)
                                    return qsTr("Disabled")
                                return qsTr("Not Set")
                            }
                            color: {
                                if (modelData.status === 2)
                                    return SemanticTokens.statusError
                                if (modelData.status === 1)
                                    return SemanticTokens.statusSuccess
                                return SemanticTokens.textTertiary
                            }
                            font.pixelSize: SemanticTokens.fontSizeBody
                            font.family: SemanticTokens.fontFamily
                            font.letterSpacing: SemanticTokens.letterSpacingDefault
                        }

                        // Action buttons (right-aligned)
                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: SemanticTokens.spacing4

                            SettingsButton {
                                text: qsTr("Edit")
                                onClicked: settingsBackend.editHotkey(modelData.action)
                            }

                            SettingsButton {
                                text: qsTr("Clear")
                                onClicked: settingsBackend.clearHotkey(modelData.action)
                            }

                            SettingsButton {
                                text: qsTr("Reset")
                                onClicked: settingsBackend.resetHotkey(modelData.action)
                            }
                        }
                    }
                }

                Spacer { size: SemanticTokens.spacing4 }
            }
        }
    }
}
