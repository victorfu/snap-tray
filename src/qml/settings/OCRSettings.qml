import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * OCRSettings: Language selection (dual-list) and OCR behavior options.
 *
 * Lazy-loads available languages on first display via settingsBackend.loadOcrLanguages().
 * Uses local properties to cache Q_INVOKABLE data and refreshes after mutations.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    // Local state caching Q_INVOKABLE return values
    property var availableLangs: []
    property var selectedLangs: []
    property int ocrBehavior: 0

    function refreshLists() {
        availableLangs = settingsBackend.ocrAvailableLanguages()
        selectedLangs = settingsBackend.ocrSelectedLanguages()
    }

    Component.onCompleted: {
        settingsBackend.loadOcrLanguages()
        ocrBehavior = settingsBackend.ocrBehavior()
        refreshLists()
    }

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: 8

        // Info label
        Text {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            text: qsTr("Select and order the languages for OCR recognition.\nEnglish is always included and cannot be removed.")
            color: SemanticTokens.textSecondary
            font.pixelSize: PrimitiveTokens.fontSizeBody
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: -0.2
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: 4 }

        // Dual-list language picker
        Row {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: 220
            spacing: 8

            // Available languages
            Column {
                width: (parent.width - transferButtons.width - 16) / 2
                height: parent.height
                spacing: 4

                Text {
                    text: qsTr("Available Languages")
                    color: SemanticTokens.textPrimary
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.weight: Font.Medium
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: -0.2
                }

                Rectangle {
                    width: parent.width
                    height: parent.height - 20
                    radius: PrimitiveTokens.radiusSmall
                    color: ComponentTokens.inputBackground
                    border.width: 1
                    border.color: ComponentTokens.inputBorder

                    ListView {
                        id: availableList
                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        model: root.availableLangs

                        property int selectedIndex: -1

                        delegate: Rectangle {
                            width: availableList.width
                            height: 28
                            radius: PrimitiveTokens.radiusSmall
                            color: availableList.selectedIndex === index
                                ? ComponentTokens.settingsSidebarActiveItem
                                : availItemHover.containsMouse
                                    ? ComponentTokens.settingsSidebarHoverItem
                                    : "transparent"

                            required property int index
                            required property var modelData

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.displayName || modelData
                                color: SemanticTokens.textPrimary
                                font.pixelSize: PrimitiveTokens.fontSizeBody
                                font.family: PrimitiveTokens.fontFamily
                                font.letterSpacing: -0.2
                                elide: Text.ElideRight
                                width: parent.width - 16
                            }

                            MouseArea {
                                id: availItemHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: availableList.selectedIndex = index
                                onDoubleClicked: {
                                    settingsBackend.addOcrLanguage(modelData.code)
                                    root.refreshLists()
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }
                }
            }

            // Transfer buttons
            Column {
                id: transferButtons
                width: 40
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                SettingsButton {
                    text: "\u2192"
                    width: 40
                    onClicked: {
                        if (availableList.selectedIndex >= 0
                            && availableList.selectedIndex < root.availableLangs.length) {
                            var code = root.availableLangs[availableList.selectedIndex].code
                            settingsBackend.addOcrLanguage(code)
                            root.refreshLists()
                        }
                    }
                }

                SettingsButton {
                    text: "\u2190"
                    width: 40
                    onClicked: {
                        if (selectedList.selectedIndex >= 0
                            && selectedList.selectedIndex < root.selectedLangs.length) {
                            var code = root.selectedLangs[selectedList.selectedIndex].code
                            settingsBackend.removeOcrLanguage(code)
                            root.refreshLists()
                        }
                    }
                }
            }

            // Selected languages
            Column {
                width: (parent.width - transferButtons.width - 16) / 2
                height: parent.height
                spacing: 4

                Text {
                    text: qsTr("Selected Languages")
                    color: SemanticTokens.textPrimary
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.weight: Font.Medium
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: -0.2
                }

                Rectangle {
                    width: parent.width
                    height: parent.height - 20
                    radius: PrimitiveTokens.radiusSmall
                    color: ComponentTokens.inputBackground
                    border.width: 1
                    border.color: ComponentTokens.inputBorder

                    ListView {
                        id: selectedList
                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        model: root.selectedLangs

                        property int selectedIndex: -1

                        delegate: Rectangle {
                            width: selectedList.width
                            height: 28
                            radius: PrimitiveTokens.radiusSmall
                            color: selectedList.selectedIndex === index
                                ? ComponentTokens.settingsSidebarActiveItem
                                : selItemHover.containsMouse
                                    ? ComponentTokens.settingsSidebarHoverItem
                                    : "transparent"

                            required property int index
                            required property var modelData

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.displayName || modelData
                                color: SemanticTokens.textPrimary
                                font.pixelSize: PrimitiveTokens.fontSizeBody
                                font.family: PrimitiveTokens.fontFamily
                                font.letterSpacing: -0.2
                                elide: Text.ElideRight
                                width: parent.width - 16
                            }

                            MouseArea {
                                id: selItemHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: selectedList.selectedIndex = index
                                onDoubleClicked: {
                                    if (modelData.code !== "en-US") {
                                        settingsBackend.removeOcrLanguage(modelData.code)
                                        root.refreshLists()
                                    }
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }
                }
            }
        }

        // OCR behavior section
        SettingsSection { title: qsTr("After OCR Recognition") }

        Rectangle {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: behaviorColumn.height + 16
            radius: PrimitiveTokens.radiusSmall
            color: ComponentTokens.inputBackground
            border.width: 1
            border.color: ComponentTokens.inputBorder

            Column {
                id: behaviorColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                spacing: 4

                // Direct copy option
                Rectangle {
                    width: parent.width
                    height: 32
                    radius: PrimitiveTokens.radiusSmall
                    color: root.ocrBehavior === 0
                        ? ComponentTokens.settingsSidebarActiveItem : "transparent"

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Rectangle {
                            width: 16
                            height: 16
                            radius: 8
                            border.width: 2
                            border.color: root.ocrBehavior === 0
                                ? SemanticTokens.accentDefault
                                : SemanticTokens.textTertiary
                            color: "transparent"
                            anchors.verticalCenter: parent.verticalCenter

                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: SemanticTokens.accentDefault
                                visible: root.ocrBehavior === 0
                            }
                        }

                        Text {
                            text: qsTr("Copy text directly to clipboard")
                            color: SemanticTokens.textPrimary
                            font.pixelSize: PrimitiveTokens.fontSizeBody
                            font.family: PrimitiveTokens.fontFamily
                            font.letterSpacing: -0.2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.ocrBehavior = 0
                            settingsBackend.setOcrBehavior(0)
                        }
                    }
                }

                // Show editor option
                Rectangle {
                    width: parent.width
                    height: 32
                    radius: PrimitiveTokens.radiusSmall
                    color: root.ocrBehavior === 1
                        ? ComponentTokens.settingsSidebarActiveItem : "transparent"

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Rectangle {
                            width: 16
                            height: 16
                            radius: 8
                            border.width: 2
                            border.color: root.ocrBehavior === 1
                                ? SemanticTokens.accentDefault
                                : SemanticTokens.textTertiary
                            color: "transparent"
                            anchors.verticalCenter: parent.verticalCenter

                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: SemanticTokens.accentDefault
                                visible: root.ocrBehavior === 1
                            }
                        }

                        Text {
                            text: qsTr("Show editor to review and edit text")
                            color: SemanticTokens.textPrimary
                            font.pixelSize: PrimitiveTokens.fontSizeBody
                            font.family: PrimitiveTokens.fontFamily
                            font.letterSpacing: -0.2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.ocrBehavior = 1
                            settingsBackend.setOcrBehavior(1)
                        }
                    }
                }
            }
        }
    }
}
