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
    property bool ocrLoading: true

    function refreshLists() {
        availableLangs = settingsBackend.ocrAvailableLanguages()
        selectedLangs = settingsBackend.ocrSelectedLanguages()
        ocrLoading = false
    }

    Connections {
        target: settingsBackend
        function onOcrLanguagesChanged() { root.refreshLists() }
    }

    Component.onCompleted: {
        settingsBackend.loadOcrLanguages()
        ocrBehavior = settingsBackend.ocrBehavior()
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
            font.pixelSize: PrimitiveTokens.fontSizeBody
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: PrimitiveTokens.letterSpacingTight
            wrapMode: Text.WordWrap
        }

        Spacer { size: PrimitiveTokens.spacing4 }

        // Loading indicator
        Item {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: 40
            visible: root.ocrLoading

            Row {
                anchors.centerIn: parent
                spacing: 8

                BusySpinner {
                    running: root.ocrLoading
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: qsTr("Loading languages...")
                    color: SemanticTokens.textSecondary
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: PrimitiveTokens.letterSpacingTight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // Dual-list language picker
        Row {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: 220
            spacing: PrimitiveTokens.spacing8
            visible: !root.ocrLoading
            opacity: root.ocrLoading ? 0 : 1

            Behavior on opacity {
                NumberAnimation { duration: PrimitiveTokens.durationNormal }
            }

            // Available languages
            Column {
                width: (parent.width - transferButtons.width - 2 * PrimitiveTokens.spacing8) / 2
                height: parent.height
                spacing: 4

                Text {
                    text: qsTr("Available Languages")
                    color: SemanticTokens.textPrimary
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.weight: Font.Medium
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: PrimitiveTokens.letterSpacingTight
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
                                font.letterSpacing: PrimitiveTokens.letterSpacingTight
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
                width: PrimitiveTokens.spacing40
                anchors.verticalCenter: parent.verticalCenter
                spacing: PrimitiveTokens.spacing8

                SettingsButton {
                    text: "\u2192"
                    width: PrimitiveTokens.spacing40
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
                    width: PrimitiveTokens.spacing40
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
                width: (parent.width - transferButtons.width - 2 * PrimitiveTokens.spacing8) / 2
                height: parent.height
                spacing: 4

                Text {
                    text: qsTr("Selected Languages")
                    color: SemanticTokens.textPrimary
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.weight: Font.Medium
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: PrimitiveTokens.letterSpacingTight
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
                                radius: PrimitiveTokens.radiusSmall

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
                                    font.pixelSize: PrimitiveTokens.fontSizeBody
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: selDelegate.modelData.displayName || selDelegate.modelData
                                    color: SemanticTokens.textPrimary
                                    font.pixelSize: PrimitiveTokens.fontSizeBody
                                    font.family: PrimitiveTokens.fontFamily
                                    font.letterSpacing: PrimitiveTokens.letterSpacingTight
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
                            font.letterSpacing: PrimitiveTokens.letterSpacingTight
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
                            font.letterSpacing: PrimitiveTokens.letterSpacingTight
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
