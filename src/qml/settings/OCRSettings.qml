import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * OCRSettings: Language selection (dual-list) and OCR behavior options.
 *
 * The page loads immediately, then fetches OCR languages in the background so
 * switching into Settings never blocks on OCR initialization.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    readonly property var availableLangs: settingsBackend.ocrAvailableLanguages
    readonly property var selectedLangs: settingsBackend.ocrSelectedLanguages
    readonly property bool ocrLoading: settingsBackend.ocrLoading
    readonly property bool ocrLoaded: settingsBackend.ocrAvailableLanguagesLoaded
    readonly property bool ocrSupported: settingsBackend.ocrSupported
    readonly property bool showLoadingState: root.ocrSupported && (!root.ocrLoaded || root.ocrLoading)
    readonly property bool showLanguageLists: root.ocrLoaded && !root.ocrLoading && root.availableLangs.length > 0

    Component.onCompleted: {
        Qt.callLater(function() {
            settingsBackend.loadOcrLanguages()
        })
    }

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: ComponentTokens.settingsColumnSpacing

        // Info label
        Text {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            text: qsTr("Select and order the languages for OCR recognition.\nEnglish is always included and cannot be removed.")
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeBody
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            wrapMode: Text.WordWrap
        }

        Spacer { size: SemanticTokens.spacing4 }

        Rectangle {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            visible: !root.showLanguageLists
            radius: SemanticTokens.radiusSmall
            color: ComponentTokens.infoPanelBg
            border.width: 1
            border.color: ComponentTokens.infoPanelBorder
            height: statusColumn.implicitHeight + 24

            Column {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: SemanticTokens.spacing8

                Row {
                    visible: root.showLoadingState
                    spacing: SemanticTokens.spacing8

                    BusySpinner {
                        running: root.showLoadingState
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: qsTr("Preparing OCR languages...")
                        color: ComponentTokens.infoPanelText
                        font.pixelSize: SemanticTokens.fontSizeBody
                        font.weight: Font.Medium
                        font.family: SemanticTokens.fontFamily
                        font.letterSpacing: SemanticTokens.letterSpacingDefault
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Text {
                    width: parent.width
                    text: root.showLoadingState
                        ? qsTr("The settings window is ready. OCR languages are loading in the background.")
                        : root.ocrSupported
                            ? qsTr("No OCR languages are available on this device right now.")
                            : qsTr("OCR is not available on this device.")
                    color: ComponentTokens.infoPanelText
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    font.letterSpacing: SemanticTokens.letterSpacingDefault
                    wrapMode: Text.WordWrap
                }
            }
        }

        // Dual-list language picker
        Row {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: 220
            spacing: SemanticTokens.spacing8
            visible: root.showLanguageLists
            opacity: root.showLanguageLists ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: SemanticTokens.durationNormal }
            }

            // Available languages
            Column {
                width: (parent.width - transferButtons.width - 2 * SemanticTokens.spacing8) / 2
                height: parent.height
                spacing: SemanticTokens.spacing4

                Text {
                    text: qsTr("Available Languages")
                    color: SemanticTokens.textPrimary
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.weight: Font.Medium
                    font.family: SemanticTokens.fontFamily
                    font.letterSpacing: SemanticTokens.letterSpacingDefault
                }

                Rectangle {
                    width: parent.width
                    height: parent.height - 20
                    radius: SemanticTokens.radiusSmall
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
                            radius: SemanticTokens.radiusSmall
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
                                font.pixelSize: SemanticTokens.fontSizeBody
                                font.family: SemanticTokens.fontFamily
                                font.letterSpacing: SemanticTokens.letterSpacingDefault
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
                width: SemanticTokens.spacing40
                anchors.verticalCenter: parent.verticalCenter
                spacing: SemanticTokens.spacing8

                SettingsButton {
                    text: "\u2192"
                    width: SemanticTokens.spacing40
                    Accessible.name: qsTr("Add selected language")
                    Accessible.role: Accessible.Button
                    onClicked: {
                        if (availableList.selectedIndex >= 0
                            && availableList.selectedIndex < root.availableLangs.length) {
                            var code = root.availableLangs[availableList.selectedIndex].code
                            settingsBackend.addOcrLanguage(code)
                        }
                    }
                }

                SettingsButton {
                    text: "\u2190"
                    width: SemanticTokens.spacing40
                    Accessible.name: qsTr("Remove selected language")
                    Accessible.role: Accessible.Button
                    onClicked: {
                        if (selectedList.selectedIndex >= 0
                            && selectedList.selectedIndex < root.selectedLangs.length) {
                            var code = root.selectedLangs[selectedList.selectedIndex].code
                            settingsBackend.removeOcrLanguage(code)
                        }
                    }
                }
            }

            // Selected languages
            Column {
                width: (parent.width - transferButtons.width - 2 * SemanticTokens.spacing8) / 2
                height: parent.height
                spacing: SemanticTokens.spacing4

                Text {
                    text: qsTr("Selected Languages")
                    color: SemanticTokens.textPrimary
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.weight: Font.Medium
                    font.family: SemanticTokens.fontFamily
                    font.letterSpacing: SemanticTokens.letterSpacingDefault
                }

                Rectangle {
                    width: parent.width
                    height: parent.height - 20
                    radius: SemanticTokens.radiusSmall
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

                        delegate: Item {
                            id: selDelegate
                            width: selectedList.width
                            height: 28
                            z: selDragArea.drag.active ? 10 : 0

                            required property int index
                            required property var modelData

                            DropArea {
                                anchors.fill: parent
                                onDropped: function(drop) {
                                    if (!drop.source || drop.source.dragIndex === undefined)
                                        return
                                    var from = drop.source.dragIndex
                                    var to = selDelegate.index
                                    if (from >= 0 && to >= 0 && from !== to)
                                        settingsBackend.moveOcrLanguage(from, to)
                                }
                            }

                            Rectangle {
                                id: selContent
                                width: selDelegate.width
                                height: selDelegate.height
                                radius: SemanticTokens.radiusSmall

                                property int dragIndex: selDelegate.index

                                color: selDragArea.drag.active
                                    ? ComponentTokens.settingsSidebarActiveItem
                                    : selectedList.selectedIndex === selDelegate.index
                                        ? ComponentTokens.settingsSidebarActiveItem
                                        : selDragArea.containsMouse
                                            ? ComponentTokens.settingsSidebarHoverItem
                                            : "transparent"

                                Drag.active: selDragArea.drag.active
                                Drag.source: selContent
                                Drag.hotSpot: Qt.point(width / 2, height / 2)

                                // Drag grip
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "\u2261"
                                    color: SemanticTokens.textTertiary
                                    font.pixelSize: SemanticTokens.fontSizeBody
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: selDelegate.modelData.displayName || selDelegate.modelData
                                    color: SemanticTokens.textPrimary
                                    font.pixelSize: SemanticTokens.fontSizeBody
                                    font.family: SemanticTokens.fontFamily
                                    font.letterSpacing: SemanticTokens.letterSpacingDefault
                                    elide: Text.ElideRight
                                    width: parent.width - 26
                                }
                            }

                            MouseArea {
                                id: selDragArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.OpenHandCursor
                                drag.target: selContent
                                drag.axis: Drag.YAxis

                                onClicked: selectedList.selectedIndex = selDelegate.index
                                onDoubleClicked: {
                                    if (selDelegate.modelData.code !== "en-US")
                                        settingsBackend.removeOcrLanguage(selDelegate.modelData.code)
                                }
                                onReleased: {
                                    if (drag.active)
                                        selContent.Drag.drop()
                                    selContent.x = 0
                                    selContent.y = 0
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

        SettingsRadioGroup {
            currentValue: settingsBackend.ocrBehavior
            model: [
                { text: qsTr("Copy text directly to clipboard"), value: 0 },
                { text: qsTr("Show editor to review and edit text"), value: 1 }
            ]
            onActivated: function(value) {
                settingsBackend.ocrBehavior = value
            }
        }
    }
}
