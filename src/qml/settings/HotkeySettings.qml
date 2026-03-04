import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * HotkeySettings: Grouped hotkey list with edit / clear / reset actions.
 *
 * Uses fixed column widths so Action, Hotkey, and Status columns align
 * across all rows regardless of text length.
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

    // Fixed column layout constants
    readonly property int colActionWidth: 190
    readonly property int colKeyWidth: 100
    readonly property int colStatusWidth: 60
    readonly property int rowLeftMargin: 8

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: 4

        // Header row with "Restore All Defaults" button
        Item {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: 36

            Text {
                text: qsTr("Keyboard Shortcuts")
                color: SemanticTokens.textPrimary
                font.pixelSize: PrimitiveTokens.fontSizeH3
                font.weight: Font.DemiBold
                font.family: PrimitiveTokens.fontFamily
                font.letterSpacing: -0.2
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

        Item { width: 1; height: 8 }

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
                    font.pixelSize: PrimitiveTokens.fontSizeCaption
                    font.weight: Font.Medium
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: 0.5
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
                        radius: PrimitiveTokens.radiusSmall
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

                        // Action name — fixed column
                        Text {
                            id: actionName
                            x: root.rowLeftMargin
                            width: root.colActionWidth
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            color: SemanticTokens.textPrimary
                            font.pixelSize: PrimitiveTokens.fontSizeBody
                            font.family: PrimitiveTokens.fontFamily
                            font.letterSpacing: -0.2
                            elide: Text.ElideRight
                        }

                        // Key sequence — fixed column
                        Text {
                            id: keySeq
                            x: root.rowLeftMargin + root.colActionWidth
                            width: root.colKeyWidth
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.keySequence || qsTr("Not Set")
                            color: modelData.keySequence
                                ? SemanticTokens.textPrimary
                                : SemanticTokens.textTertiary
                            font.pixelSize: PrimitiveTokens.fontSizeBody
                            font.family: PrimitiveTokens.fontFamily
                            font.letterSpacing: -0.2
                        }

                        // Status text — fixed column
                        // HotkeyStatus enum: Unset=0, Registered=1, Failed=2, Disabled=3
                        Text {
                            id: statusText
                            x: root.rowLeftMargin + root.colActionWidth + root.colKeyWidth
                            width: root.colStatusWidth
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                if (modelData.status === 2)
                                    return qsTr("Conflict")
                                if (modelData.status === 1)
                                    return qsTr("Active")
                                return qsTr("Not Set")
                            }
                            color: {
                                if (modelData.status === 2)
                                    return SemanticTokens.statusError
                                if (modelData.status === 1)
                                    return SemanticTokens.statusSuccess
                                return SemanticTokens.textTertiary
                            }
                            font.pixelSize: PrimitiveTokens.fontSizeBody
                            font.family: PrimitiveTokens.fontFamily
                            font.letterSpacing: -0.2
                        }

                        // Action buttons (right-aligned)
                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4

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

                Item { width: 1; height: 4 }
            }
        }
    }
}
