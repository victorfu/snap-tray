import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SnapTrayQml

// The Library (#226): screenshots (HistoryModel/HistoryBackend) and editable
// recording projects (RecordingLibraryViewModel) in one window. The list is
// libraryModel; each row's sourceRow addresses its own backend.
Item {
    id: root

    readonly property var modelObject: typeof historyModel !== "undefined" ? historyModel : null
    readonly property var backend: typeof historyBackend !== "undefined" ? historyBackend : null
    readonly property var library: typeof libraryModel !== "undefined" ? libraryModel : null
    readonly property var recordings: typeof recordingLibrary !== "undefined" ? recordingLibrary : null

    // Library sources (LibraryModel::Source) and kinds (LibraryModel::Kind).
    readonly property int sourceAll: 0
    readonly property int sourceScreenshots: 1
    readonly property int sourceRecordings: 2
    readonly property int kindScreenshot: 0
    readonly property int kindRecording: 1

    property string selectedKey: ""
    property int selectedIndex: -1
    property bool listMode: false
    // The key whose delete waits for confirmation, or "".
    property string confirmKey: ""
    readonly property bool confirming: confirmKey !== ""

    readonly property int currentSource: library ? library.source : sourceAll
    // Rows are read by index; revision re-reads them after every rebuild.
    readonly property int selectedKind: library && selectedIndex >= 0
        ? (library.revision, library.kindAt(selectedIndex)) : -1
    readonly property int selectedSourceRow: library && selectedIndex >= 0
        ? (library.revision, library.sourceRowAt(selectedIndex)) : -1
    readonly property bool selectedBusy: selectedKind === kindRecording
        && (library.revision, libraryRole(selectedIndex, "busy") === true)
    readonly property bool selectedSavedExists: selectedKind === kindRecording
        && (library.revision, libraryRole(selectedIndex, "savedFileExists") === true)

    readonly property bool hasAnything: library && library.allCount > 0
    readonly property bool hasVisibleResults: library && library.count > 0
    readonly property bool narrow: width < 820
    readonly property string currentTitle: sectionTitle()
    readonly property int responsiveSidebarWidth: Math.max(180, Math.min(244, Math.floor(mainRow.width * 0.23)))
    readonly property bool showProjectNote: currentSource === sourceRecordings
        || (currentSource === sourceAll && selectedKind === kindRecording)
    readonly property int cleanupDays: recordings ? recordings.cleanupDays : 0

    property var sections: [
        { key: "all", text: qsTr("All Captures"), source: 0 },
        { key: "screenshots", text: qsTr("Screenshots"), source: 1 },
        { key: "recordings", text: qsTr("Recordings"), source: 2 }
    ]

    property var smartFolders: [
        { text: qsTr("Last 7 Days"), filterValue: 1 },
        { text: qsTr("Large Files"), filterValue: 2 },
        { text: qsTr("Replayable"), filterValue: 3 }
    ]

    property var sortOptions: [
        { text: qsTr("Newest First"), value: 0 },
        { text: qsTr("Oldest First"), value: 1 },
        { text: qsTr("Largest First"), value: 2 }
    ]

    // The command bar and context menu for each kind; Return opens the first.
    readonly property var screenshotActions: [
        { id: "edit", text: qsTr("Edit"), icon: "qrc:/icons/icons/pencil.svg" },
        { id: "pin", text: qsTr("Pin"), icon: "qrc:/icons/icons/pin.svg" },
        { id: "copy", text: qsTr("Copy"), icon: "qrc:/icons/icons/copy.svg" },
        { id: "save", text: qsTr("Save As..."), icon: "qrc:/icons/icons/save.svg" },
        { id: "delete", text: qsTr("Delete..."), icon: "qrc:/icons/icons/trash-2.svg", danger: true }
    ]
    readonly property var recordingActions: [
        { id: "open", text: qsTr("Open in Editor"), icon: "qrc:/icons/icons/pencil.svg" },
        { id: "showSaved", text: qsTr("Show Saved File"), icon: "" },
        { id: "showFolder", text: qsTr("Show Project Folder"), icon: "" },
        { id: "deleteProject", text: qsTr("Delete Project..."), icon: "qrc:/icons/icons/trash-2.svg", danger: true }
    ]

    focus: true

    function libraryRole(row, role) {
        if (!library || row < 0)
            return undefined
        return library.roleAt(row, role)
    }

    function sectionTitle() {
        if (currentSource === sourceRecordings)
            return qsTr("Recordings")
        if (currentSource === sourceAll)
            return qsTr("All Captures")
        switch (modelObject ? modelObject.activeFilter : 0) {
        case 1:
            return qsTr("Last 7 Days")
        case 2:
            return qsTr("Large Files")
        case 3:
            return qsTr("Replayable")
        default:
            return qsTr("Screenshots")
        }
    }

    function showSection(source) {
        confirmKey = ""
        if (modelObject && source !== sourceRecordings)
            modelObject.activeFilter = 0
        if (library)
            library.source = source
    }

    function showSmartFolder(filterValue) {
        confirmKey = ""
        if (modelObject)
            modelObject.activeFilter = filterValue
        if (library)
            library.source = sourceScreenshots
    }

    function selectKey(key) {
        if (key !== selectedKey)
            confirmKey = ""
        selectedKey = key
        selectedIndex = library ? library.indexOfKey(key) : -1
    }

    function syncSelection() {
        if (!library || library.count === 0) {
            selectedKey = ""
            selectedIndex = -1
            confirmKey = ""
            return
        }
        var next = selectedKey !== "" ? library.indexOfKey(selectedKey) : -1
        if (next < 0) {
            next = 0
            selectedKey = library.keyAt(0)
        }
        selectedIndex = next
        if (confirmKey !== "" && library.indexOfKey(confirmKey) < 0)
            confirmKey = ""
    }

    function openContextMenu(globalX, globalY, key) {
        selectKey(key)
        contextMenu.model = selectedKind === kindRecording ? recordingMenuModel() : screenshotMenuModel()
        var localPoint = root.mapFromGlobal(globalX, globalY)
        contextMenu.x = Math.min(localPoint.x, root.width - contextMenu.width - 8)
        contextMenu.y = Math.min(localPoint.y, root.height - contextMenu.height - 8)
        contextMenu.open()
    }

    function menuModel(actions) {
        var items = []
        for (var i = 0; i < actions.length; ++i) {
            if (actions[i].danger)
                items.push({ separator: true })
            if (actionEnabled(actions[i].id))
                items.push({ id: actions[i].id, text: actions[i].text, icon: actions[i].icon })
        }
        return items
    }
    function screenshotMenuModel() { return menuModel(screenshotActions) }
    function recordingMenuModel() { return menuModel(recordingActions) }

    function actionEnabled(actionId) {
        if (selectedIndex < 0)
            return false
        if (actionId === "open" || actionId === "deleteProject")
            return !selectedBusy
        if (actionId === "showSaved")
            return selectedSavedExists
        return true
    }

    function triggerAction(actionId) {
        if (selectedIndex < 0 || !actionEnabled(actionId))
            return
        var row = selectedSourceRow
        if (selectedKind === kindScreenshot && backend) {
            if (actionId === "edit")
                backend.edit(row)
            else if (actionId === "pin")
                backend.pin(row)
            else if (actionId === "copy")
                backend.copy(row)
            else if (actionId === "save")
                backend.saveAs(row)
            else if (actionId === "delete")
                askDelete()
        } else if (selectedKind === kindRecording && recordings) {
            if (actionId === "open")
                recordings.open(row)
            else if (actionId === "showSaved")
                recordings.showSavedFile(row)
            else if (actionId === "showFolder")
                recordings.showInFolder(row)
            else if (actionId === "deleteProject")
                askDelete()
        }
    }

    function openSelected() {
        triggerAction(selectedKind === kindRecording ? "open" : "edit")
    }

    function askDelete() {
        if (selectedIndex < 0 || (selectedKind === kindRecording && selectedBusy))
            return
        confirmKey = selectedKey
        confirmDeleteButton.forceActiveFocus()
    }

    function cancelDelete() {
        confirmKey = ""
        root.forceActiveFocus()
    }

    // Every path that drops the question (a new selection, section or smart
    // folder, a rebuild, a project opened in the preview) only clears
    // confirmKey. Take focus back from the hidden strip so Return opens the
    // selection instead of reaching its Delete button.
    onConfirmingChanged: {
        if (!confirming && (confirmDeleteButton.activeFocus || confirmCancelButton.activeFocus))
            root.forceActiveFocus()
    }

    // A project opened in the preview cannot be deleted: drop the question.
    onSelectedBusyChanged: if (selectedBusy && confirming) confirmKey = ""

    function confirmDelete() {
        if (!confirming)
            return
        if (selectedKind === kindRecording && selectedBusy) {
            cancelDelete()
            return
        }
        var index = library ? library.indexOfKey(confirmKey) : -1
        var kind = index >= 0 ? library.kindAt(index) : -1
        var row = index >= 0 ? library.sourceRowAt(index) : -1
        confirmKey = ""
        root.forceActiveFocus()
        if (row < 0)
            return
        if (kind === kindScreenshot && backend)
            backend.deleteEntry(row)
        else if (kind === kindRecording && recordings)
            recordings.remove(row)
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape && confirming) {
            cancelDelete()
            event.accepted = true
            return
        }
        if (selectedIndex < 0 || confirming)
            return
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            openSelected()
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            askDelete()
            event.accepted = true
        }
    }

    Connections {
        target: root.library

        function onModelReset() {
            root.syncSelection()
        }
    }

    Component.onCompleted: syncSelection()

    component GridGlyph: Canvas {
        property color strokeColor: SemanticTokens.textSecondary
        implicitWidth: 16
        implicitHeight: 16
        width: implicitWidth
        height: implicitHeight
        antialiasing: true
        renderTarget: Canvas.Image

        onStrokeColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = strokeColor
            var size = Math.min(width, height) * 0.32
            var gap = Math.min(width, height) * 0.14
            var startX = (width - (size * 2 + gap)) / 2
            var startY = (height - (size * 2 + gap)) / 2
            for (var row = 0; row < 2; ++row) {
                for (var col = 0; col < 2; ++col) {
                    ctx.fillRect(startX + col * (size + gap),
                                 startY + row * (size + gap),
                                 size,
                                 size)
                }
            }
        }

        Component.onCompleted: requestPaint()
    }

    component ListGlyph: Canvas {
        property color strokeColor: SemanticTokens.textSecondary
        implicitWidth: 16
        implicitHeight: 16
        width: implicitWidth
        height: implicitHeight
        antialiasing: true
        renderTarget: Canvas.Image

        onStrokeColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = strokeColor
            var barHeight = Math.max(2, Math.floor(height * 0.14))
            var gap = Math.floor(height * 0.16)
            var startX = Math.floor(width * 0.12)
            var barWidth = Math.floor(width * 0.76)
            for (var i = 0; i < 3; ++i) {
                ctx.fillRect(startX,
                             Math.floor(height * 0.16) + i * (barHeight + gap),
                             barWidth,
                             barHeight)
            }
        }

        Component.onCompleted: requestPaint()
    }

    component HistoryActionButton: Rectangle {
        id: actionButton

        property string textLabel: ""
        property url iconSource: ""
        property bool danger: false
        property bool enabledButton: true

        signal clicked()

        implicitHeight: 34
        implicitWidth: Math.max(90, label.implicitWidth + (icon.visible ? 48 : 26))
        Accessible.role: Accessible.Button
        Accessible.name: textLabel
        Accessible.onPressAction: if (enabledButton) clicked()
        radius: 11
        color: {
            if (!enabledButton)
                return "transparent"
            if (actionMouse.pressed)
                return danger ? Qt.rgba(0.94, 0.27, 0.27, SemanticTokens.isDarkMode ? 0.28 : 0.14)
                              : ComponentTokens.toolbarPopupSelected
            if (actionMouse.containsMouse)
                return danger ? Qt.rgba(0.94, 0.27, 0.27, SemanticTokens.isDarkMode ? 0.18 : 0.10)
                              : ComponentTokens.toolbarPopupHover
            return "transparent"
        }
        border.width: activeFocus ? 2 : (enabledButton && (actionMouse.containsMouse || actionMouse.pressed) ? 1 : 0)
        border.color: danger ? SemanticTokens.statusError : ComponentTokens.toolbarPopupSelectedBorder
        opacity: enabledButton ? 1.0 : 0.42

        Behavior on color {
            ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
        }

        Row {
            anchors.centerIn: parent
            spacing: 8

            SvgIcon {
                id: icon
                visible: actionButton.iconSource !== ""
                source: actionButton.iconSource
                iconSize: ComponentTokens.iconSizeMenu
                color: actionButton.danger ? SemanticTokens.statusError : SemanticTokens.iconColor
            }

            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                text: actionButton.textLabel
                color: actionButton.danger ? SemanticTokens.statusError : SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeBody
                font.family: SemanticTokens.fontFamily
                font.weight: SemanticTokens.fontWeightMedium
                elide: Text.ElideRight
            }
        }

        MouseArea {
            id: actionMouse
            anchors.fill: parent
            enabled: actionButton.enabledButton
            hoverEnabled: true
            cursorShape: actionButton.enabledButton ? CursorTokens.clickable : CursorTokens.defaultCursor
            onClicked: actionButton.clicked()
        }
    }

    component HistorySidebarButton: Rectangle {
        id: sidebarButton

        property string textLabel: ""
        property string countLabel: ""
        property bool active: false

        signal clicked()

        height: 44
        radius: 12
        Accessible.role: Accessible.Button
        Accessible.name: countLabel !== "" ? textLabel + ", " + countLabel : textLabel
        Accessible.checkable: true
        Accessible.checked: active
        Accessible.onPressAction: clicked()
        color: {
            if (sidebarMouse.pressed)
                return ComponentTokens.settingsSidebarActiveItem
            if (active)
                return ComponentTokens.settingsSidebarActiveItem
            if (sidebarMouse.containsMouse)
                return ComponentTokens.settingsSidebarHoverItem
            return "transparent"
        }

        Behavior on color {
            ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 22
            radius: 1.5
            color: SemanticTokens.accentDefault
            opacity: sidebarButton.active ? 1.0 : 0.0

            Behavior on opacity {
                NumberAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
            }
        }

        Text {
            id: countText
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: sidebarButton.countLabel
            color: SemanticTokens.textTertiary
            font.pixelSize: SemanticTokens.fontSizeBody
            font.family: SemanticTokens.fontFamily
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.right: countText.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: sidebarButton.textLabel
            color: sidebarButton.active ? SemanticTokens.textPrimary : SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeBody
            font.family: SemanticTokens.fontFamily
            font.weight: sidebarButton.active ? SemanticTokens.fontWeightMedium : SemanticTokens.fontWeightRegular
            elide: Text.ElideRight
        }

        MouseArea {
            id: sidebarMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: CursorTokens.clickable
            onClicked: sidebarButton.clicked()
        }
    }

    component HistoryToggleButton: Rectangle {
        id: toggleButton

        default property alias contentData: contentHolder.data

        property bool selected: false
        property bool enabledButton: true

        signal clicked()

        implicitWidth: 38
        implicitHeight: 36
        radius: 11
        color: {
            if (!enabledButton)
                return "transparent"
            if (toggleMouse.pressed || selected)
                return ComponentTokens.toolbarPopupSelected
            if (toggleMouse.containsMouse)
                return ComponentTokens.toolbarPopupHover
            return "transparent"
        }
        border.width: selected ? 1 : 0
        border.color: ComponentTokens.toolbarPopupSelectedBorder
        opacity: enabledButton ? 1.0 : 0.42

        Behavior on color {
            ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
        }

        Item {
            id: contentHolder
            anchors.centerIn: parent
            width: 16
            height: 16
        }

        MouseArea {
            id: toggleMouse
            anchors.fill: parent
            enabled: toggleButton.enabledButton
            hoverEnabled: true
            cursorShape: toggleButton.enabledButton ? CursorTokens.clickable : CursorTokens.defaultCursor
            onClicked: toggleButton.clicked()
        }
    }

    component HistoryGridCard: Rectangle {
        id: gridCard

        property string entryKey: ""
        property url thumbnailUrl
        property string titleText: ""
        property string kindText: ""
        property string metaText: ""
        property var detailLines: []
        property bool selected: false

        signal selectedEntry(string entryKey)
        signal openRequested(string entryKey)
        signal contextMenuRequested(real globalX, real globalY, string entryKey)

        Accessible.role: Accessible.ListItem
        Accessible.name: titleText + ", " + kindText
        Accessible.description: metaText
        Accessible.selected: selected

        radius: 18
        color: gridMouse.pressed
            ? ComponentTokens.thumbnailCardSelected
            : selected
                ? ComponentTokens.thumbnailCardSelected
                : gridMouse.containsMouse
                    ? ComponentTokens.thumbnailCardHover
                    : ComponentTokens.thumbnailCardBackground
        border.width: 1
        border.color: selected
            ? ComponentTokens.thumbnailCardSelectedBorder
            : ComponentTokens.thumbnailCardBorder

        Behavior on color {
            ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
        }

        Behavior on border.color {
            ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
        }

        Rectangle {
            id: imageFrame
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            height: 156
            radius: 14
            color: ComponentTokens.beautifyPreviewPlaceholder
            border.width: 1
            border.color: ComponentTokens.beautifyPreviewFrame
            clip: true

            Image {
                id: preview
                anchors.fill: parent
                anchors.margins: 1
                source: gridCard.thumbnailUrl
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                smooth: true
            }

            Text {
                visible: preview.status === Image.Error
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Preview unavailable")
                color: SemanticTokens.textSecondary
                font.pixelSize: SemanticTokens.fontSizeBody
                font.family: SemanticTokens.fontFamily
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 14
            spacing: 5

            Text {
                width: parent.width
                text: gridCard.titleText
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeH2
                font.family: SemanticTokens.fontFamily
                font.weight: SemanticTokens.fontWeightSemiBold
                elide: Text.ElideRight
            }

            KindChip {
                text: gridCard.kindText
            }

            Text {
                text: gridCard.metaText
                color: SemanticTokens.textSecondary
                font.pixelSize: SemanticTokens.fontSizeBody
                font.family: SemanticTokens.fontFamily
                elide: Text.ElideRight
            }

            Repeater {
                model: gridCard.detailLines

                delegate: Text {
                    required property var modelData

                    width: parent.width
                    text: modelData
                    color: SemanticTokens.textTertiary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                    elide: Text.ElideRight
                }
            }
        }

        MouseArea {
            id: gridMouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true
            cursorShape: CursorTokens.clickable

            onClicked: function(mouse) {
                gridCard.selectedEntry(gridCard.entryKey)
                if (mouse.button === Qt.RightButton) {
                    var point = gridCard.mapToGlobal(mouse.x, mouse.y)
                    gridCard.contextMenuRequested(point.x, point.y, gridCard.entryKey)
                }
            }

            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton)
                    gridCard.openRequested(gridCard.entryKey)
            }
        }
    }

    component HistoryListRow: Rectangle {
        id: listRow

        property string entryKey: ""
        property url thumbnailUrl
        property string titleText: ""
        property string kindText: ""
        property string metaText: ""
        property string tooltipText: ""
        property bool selected: false

        signal selectedEntry(string entryKey)
        signal openRequested(string entryKey)
        signal contextMenuRequested(real globalX, real globalY, string entryKey)

        Accessible.role: Accessible.ListItem
        Accessible.name: titleText + ", " + kindText
        Accessible.description: metaText
        Accessible.selected: selected

        radius: 16
        color: listMouse.pressed
            ? ComponentTokens.thumbnailCardSelected
            : selected
                ? ComponentTokens.thumbnailCardSelected
                : listMouse.containsMouse
                    ? ComponentTokens.thumbnailCardHover
                    : ComponentTokens.thumbnailCardBackground
        border.width: 1
        border.color: selected
            ? ComponentTokens.thumbnailCardSelectedBorder
            : ComponentTokens.thumbnailCardBorder

        ToolTip.visible: listMouse.containsMouse && tooltipText !== ""
        ToolTip.text: tooltipText

        Behavior on color {
            ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
        }

        Behavior on border.color {
            ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
        }

        Row {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 16

            Rectangle {
                width: 136
                height: parent.height - 2
                radius: 12
                color: ComponentTokens.beautifyPreviewPlaceholder
                border.width: 1
                border.color: ComponentTokens.beautifyPreviewFrame
                clip: true

                Image {
                    id: listPreview
                    anchors.fill: parent
                    anchors.margins: 1
                    source: listRow.thumbnailUrl
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    smooth: true
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 152
                spacing: 8

                Row {
                    width: parent.width
                    spacing: 8

                    Text {
                        width: Math.min(implicitWidth, parent.width - listKind.width - parent.spacing)
                        anchors.verticalCenter: parent.verticalCenter
                        text: listRow.titleText
                        color: SemanticTokens.textPrimary
                        font.pixelSize: SemanticTokens.fontSizeH2
                        font.family: SemanticTokens.fontFamily
                        font.weight: SemanticTokens.fontWeightSemiBold
                        elide: Text.ElideRight
                    }

                    KindChip {
                        id: listKind
                        anchors.verticalCenter: parent.verticalCenter
                        text: listRow.kindText
                    }
                }

                Text {
                    width: parent.width
                    text: listRow.metaText
                    color: SemanticTokens.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    visible: text !== ""
                    text: listRow.tooltipText.split("\n")[0]
                    color: SemanticTokens.textTertiary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                    elide: Text.ElideRight
                }
            }
        }

        MouseArea {
            id: listMouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true
            cursorShape: CursorTokens.clickable

            onClicked: function(mouse) {
                listRow.selectedEntry(listRow.entryKey)
                if (mouse.button === Qt.RightButton) {
                    var point = listRow.mapToGlobal(mouse.x, mouse.y)
                    listRow.contextMenuRequested(point.x, point.y, listRow.entryKey)
                }
            }

            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton)
                    listRow.openRequested(listRow.entryKey)
            }
        }
    }


    // "Screenshot" or "Editable project": what the row is, in words.
    component KindChip: Rectangle {
        property alias text: chipText.text
        implicitWidth: chipText.implicitWidth + 12
        implicitHeight: chipText.implicitHeight + 4
        width: implicitWidth
        height: implicitHeight
        radius: SemanticTokens.radiusSmall
        color: ComponentTokens.toolbarControlBackground
        border.width: 1
        border.color: ComponentTokens.panelGlassBorder

        Text {
            id: chipText
            anchors.centerIn: parent
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.weight: SemanticTokens.fontWeightMedium
        }
    }


    component EmptyState: Item {
        property string title: ""
        property string message: ""

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 380)
            spacing: 10

            Text {
                width: parent.width
                text: parent.parent.title
                color: SemanticTokens.textPrimary
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: SemanticTokens.fontSizeH1
                font.family: SemanticTokens.fontFamily
                font.weight: SemanticTokens.fontWeightSemiBold
            }

            Text {
                width: parent.width
                text: parent.parent.message
                color: SemanticTokens.textSecondary
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: SemanticTokens.fontSizeBody
                font.family: SemanticTokens.fontFamily
            }
        }
    }

    Component {
        id: emptyResultsComponent

        EmptyState {
            objectName: "libraryEmpty"
            anchors.fill: parent
            title: root.currentSource === root.sourceRecordings
                ? qsTr("No editable projects")
                : !root.hasAnything
                    ? qsTr("Nothing here yet")
                    : qsTr("No results")
            message: root.currentSource === root.sourceRecordings
                ? qsTr("Turn on Keep editable projects in Settings > Recording to edit recordings again after saving.")
                : !root.hasAnything
                    ? qsTr("Screenshots you capture in SnapTray, and recordings you keep as editable projects, will appear here.")
                    : qsTr("Try a different section or smart folder.")
        }
    }

    Component {
        id: gridResultsComponent

        GridView {
            id: gridView
            objectName: "libraryGrid"
            anchors.fill: parent
            clip: true
            model: root.library
            currentIndex: root.selectedIndex
            cellWidth: width > 0 ? Math.max(260, Math.floor(width / Math.max(1, Math.floor(width / 300)))) : 300
            cellHeight: 350
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: root.currentTitle
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                required property string key
                required property string title
                required property string kindLabel
                required property string metaText
                required property string detailText
                required property url thumbnailUrl

                width: gridView.cellWidth
                height: gridView.cellHeight

                HistoryGridCard {
                    anchors.fill: parent
                    anchors.margins: 9
                    entryKey: parent.key
                    thumbnailUrl: parent.thumbnailUrl
                    titleText: parent.title
                    kindText: parent.kindLabel
                    metaText: parent.metaText
                    detailLines: parent.detailText ? parent.detailText.split("\n") : []
                    selected: root.selectedKey === parent.key

                    onSelectedEntry: function(entryKey) { root.selectKey(entryKey) }
                    onOpenRequested: function(entryKey) {
                        root.selectKey(entryKey)
                        root.openSelected()
                    }
                    onContextMenuRequested: function(globalX, globalY, entryKey) {
                        root.openContextMenu(globalX, globalY, entryKey)
                    }
                }
            }
        }
    }

    Component {
        id: listResultsComponent

        ListView {
            id: listView
            objectName: "libraryListView"
            anchors.fill: parent
            clip: true
            spacing: 12
            model: root.library
            currentIndex: root.selectedIndex
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: root.currentTitle
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                required property string key
                required property string title
                required property string kindLabel
                required property string metaText
                required property string detailText
                required property url thumbnailUrl

                width: listView.width
                height: 104

                HistoryListRow {
                    anchors.fill: parent
                    entryKey: parent.key
                    thumbnailUrl: parent.thumbnailUrl
                    titleText: parent.title
                    kindText: parent.kindLabel
                    metaText: parent.metaText
                    tooltipText: parent.detailText
                    selected: root.selectedKey === parent.key

                    onSelectedEntry: function(entryKey) { root.selectKey(entryKey) }
                    onOpenRequested: function(entryKey) {
                        root.selectKey(entryKey)
                        root.openSelected()
                    }
                    onContextMenuRequested: function(globalX, globalY, entryKey) {
                        root.openContextMenu(globalX, globalY, entryKey)
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: SemanticTokens.backgroundPrimary
    }

    PanelSurface {
        id: shell
        anchors.fill: parent
        anchors.margins: root.narrow ? 8 : 12
        radius: 20
    }

    Row {
        id: mainRow
        anchors.fill: shell
        anchors.margins: root.narrow ? 12 : 18
        spacing: root.narrow ? 0 : 18

        Rectangle {
            id: sidebar
            objectName: "librarySidebar"
            visible: !root.narrow
            width: visible ? root.responsiveSidebarWidth : 0
            height: parent.height
            radius: 18
            color: SemanticTokens.isDarkMode
                ? Qt.rgba(1, 1, 1, 0.03)
                : Qt.rgba(0, 0, 0, 0.02)
            border.width: 1
            border.color: ComponentTokens.panelGlassBorder

            Column {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 18

                Row {
                    width: parent.width
                    spacing: 12

                    Image {
                        width: 34
                        height: 34
                        source: "qrc:/icons/icons/snaptray.png"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("SnapTray")
                        color: SemanticTokens.textPrimary
                        font.pixelSize: 24
                        font.family: SemanticTokens.fontFamily
                        font.weight: SemanticTokens.fontWeightSemiBold
                    }
                }

                Column {
                    width: parent.width
                    spacing: 10

                    Text {
                        text: qsTr("Library")
                        color: SemanticTokens.textTertiary
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.family: SemanticTokens.fontFamily
                        font.weight: SemanticTokens.fontWeightSemiBold
                        font.letterSpacing: SemanticTokens.letterSpacingWide
                    }

                    Repeater {
                        model: root.sections

                        delegate: HistorySidebarButton {
                            required property var modelData
                            objectName: "librarySection_" + modelData.key
                            width: parent.width
                            textLabel: modelData.text
                            countLabel: !root.library ? "0"
                                : String(modelData.source === root.sourceAll ? root.library.allCount
                                    : modelData.source === root.sourceScreenshots ? root.library.screenshotCount
                                    : root.library.recordingCount)
                            active: root.currentSource === modelData.source
                                && (modelData.source !== root.sourceScreenshots
                                    || !root.modelObject || root.modelObject.activeFilter === 0)
                            onClicked: root.showSection(modelData.source)
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: SemanticTokens.borderDefault
                    opacity: 0.6
                }

                Column {
                    width: parent.width
                    spacing: 10

                    Text {
                        text: qsTr("Smart Folders")
                        color: SemanticTokens.textTertiary
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.family: SemanticTokens.fontFamily
                        font.weight: SemanticTokens.fontWeightSemiBold
                        font.letterSpacing: SemanticTokens.letterSpacingWide
                    }

                    Repeater {
                        model: root.smartFolders

                        delegate: HistorySidebarButton {
                            required property var modelData
                            objectName: "librarySmartFolder_" + modelData.filterValue
                            width: parent.width
                            textLabel: modelData.text
                            countLabel: root.modelObject
                                ? (root.modelObject.filterCountsRevision,
                                   String(root.modelObject.countForFilter(modelData.filterValue)))
                                : "0"
                            active: root.currentSource === root.sourceScreenshots && root.modelObject
                                ? root.modelObject.activeFilter === modelData.filterValue : false
                            onClicked: root.showSmartFolder(modelData.filterValue)
                        }
                    }
                }
            }
        }

        Rectangle {
            id: verticalDivider
            visible: !root.narrow
            width: visible ? 1 : 0
            height: parent.height
            color: SemanticTokens.borderDefault
            opacity: 0.45
        }

        Item {
            id: contentArea
            width: parent.width - sidebar.width - verticalDivider.width - (root.narrow ? 0 : mainRow.spacing * 2)
            height: parent.height

            Column {
                id: contentColumn
                anchors.fill: parent
                spacing: root.narrow ? 12 : 18

                // Narrow windows: the sidebar's sections as one row.
                Row {
                    id: sectionRow
                    objectName: "librarySectionRow"
                    visible: root.narrow
                    width: parent.width
                    height: visible ? 40 : 0
                    spacing: 6

                    Repeater {
                        model: root.sections

                        delegate: HistoryActionButton {
                            required property var modelData
                            objectName: "libraryNarrowSection_" + modelData.key
                            width: (sectionRow.width - sectionRow.spacing * 2) / 3
                            implicitHeight: 40
                            textLabel: modelData.text
                            border.width: root.currentSource === modelData.source ? 1 : 0
                            color: root.currentSource === modelData.source
                                ? ComponentTokens.toolbarPopupSelected : "transparent"
                            Accessible.checkable: true
                            Accessible.checked: root.currentSource === modelData.source
                            onClicked: root.showSection(modelData.source)
                        }
                    }
                }

                Item {
                    id: commandBar
                    width: parent.width
                    height: actionsFlow.implicitHeight + 20

                    GlassSurface {
                        anchors.fill: parent
                        glassBg: ComponentTokens.toolbarPopupBackground
                        glassBgTop: ComponentTokens.toolbarPopupBackgroundTop
                        glassHighlight: ComponentTokens.toolbarPopupHighlight
                        glassBorder: ComponentTokens.toolbarPopupBorder
                        glassRadius: 18
                    }

                    Flow {
                        id: actionsFlow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Repeater {
                            model: root.screenshotActions

                            delegate: HistoryActionButton {
                                required property var modelData
                                objectName: "libraryAction_" + modelData.id
                                visible: root.selectedKind !== root.kindRecording
                                textLabel: modelData.text
                                iconSource: modelData.icon
                                danger: modelData.danger === true
                                enabledButton: (root.selectedIndex, root.selectedKind, root.actionEnabled(modelData.id))
                                onClicked: root.triggerAction(modelData.id)
                            }
                        }

                        Repeater {
                            model: root.recordingActions

                            delegate: HistoryActionButton {
                                required property var modelData
                                objectName: "libraryAction_" + modelData.id
                                visible: root.selectedKind === root.kindRecording
                                textLabel: modelData.text
                                iconSource: modelData.icon
                                danger: modelData.danger === true
                                enabledButton: (root.selectedIndex, root.selectedBusy, root.selectedSavedExists,
                                                root.actionEnabled(modelData.id))
                                onClicked: root.triggerAction(modelData.id)
                            }
                        }

                    }
                }

                Item {
                    id: headerRow
                    width: parent.width
                    readonly property bool compactLayout: width < 720
                    height: compactLayout ? 96 : 52

                    Column {
                        anchors.left: parent.left
                        anchors.right: headerRow.compactLayout ? parent.right : headerControls.left
                        anchors.rightMargin: headerRow.compactLayout ? 0 : 12
                        y: headerRow.compactLayout ? 0 : Math.max(0, (headerRow.height - height) / 2)
                        spacing: 4

                        Row {
                            spacing: 8

                            Text {
                                text: qsTr("Library")
                                color: SemanticTokens.textTertiary
                                font.pixelSize: SemanticTokens.fontSizeCaption
                                font.family: SemanticTokens.fontFamily
                                font.weight: SemanticTokens.fontWeightSemiBold
                                font.letterSpacing: SemanticTokens.letterSpacingWide
                            }

                            // What the command bar acts on.
                            Text {
                                id: selectionLabel
                                objectName: "librarySelectionLabel"
                                text: root.selectedKind === root.kindRecording ? qsTr("Editable project selected")
                                    : root.selectedKind === root.kindScreenshot ? qsTr("Screenshot selected")
                                    : qsTr("Nothing selected")
                                color: SemanticTokens.textTertiary
                                font.pixelSize: SemanticTokens.fontSizeCaption
                                font.family: SemanticTokens.fontFamily
                            }
                        }

                        Text {
                            objectName: "libraryTitle"
                            width: parent.width
                            text: root.currentTitle
                            color: SemanticTokens.textPrimary
                            font.pixelSize: headerRow.compactLayout ? 26 : 34
                            font.family: SemanticTokens.fontFamily
                            font.weight: SemanticTokens.fontWeightSemiBold
                            elide: Text.ElideRight
                        }
                    }

                    Row {
                        id: headerControls
                        anchors.right: parent.right
                        y: headerRow.compactLayout ? headerRow.height - height : Math.max(0, (headerRow.height - height) / 2)
                        spacing: headerRow.compactLayout ? 8 : 10

                        SettingsButton {
                            visible: root.currentSource !== root.sourceRecordings
                            height: 40
                            radius: 14
                            text: qsTr("Open History Folder")
                            onClicked: if (root.backend) root.backend.openHistoryFolder()
                        }

                        // Narrow windows have no sidebar: smart folders live here.
                        ComboBox {
                            id: narrowFilter
                            objectName: "libraryNarrowFilter"
                            visible: root.narrow && root.currentSource === root.sourceScreenshots
                            width: 150
                            height: 40
                            model: [{ text: qsTr("All Screenshots"), filterValue: 0 }].concat(root.smartFolders)
                            textRole: "text"
                            currentIndex: root.modelObject ? root.modelObject.activeFilter : 0
                            Accessible.name: qsTr("Smart Folders")

                            onActivated: function(index) {
                                var value = model[index].filterValue
                                if (value === 0)
                                    root.showSection(root.sourceScreenshots)
                                else
                                    root.showSmartFolder(value)
                            }

                            background: Rectangle {
                                radius: 12
                                color: ComponentTokens.toolbarControlBackground
                                border.width: narrowFilter.activeFocus ? 1 : 0
                                border.color: ComponentTokens.inputBorderFocus
                            }

                            contentItem: Text {
                                leftPadding: 12
                                rightPadding: 26
                                text: narrowFilter.displayText
                                color: SemanticTokens.textPrimary
                                font.pixelSize: SemanticTokens.fontSizeBody
                                font.family: SemanticTokens.fontFamily
                                font.weight: SemanticTokens.fontWeightMedium
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            indicator: ToolbarChevron {
                                x: narrowFilter.width - width - 10
                                anchors.verticalCenter: parent.verticalCenter
                                strokeColor: SemanticTokens.textSecondary
                            }
                        }

                        ComboBox {
                            id: sortCombo
                            width: headerRow.compactLayout ? 138 : 156
                            height: 40
                            model: root.sortOptions
                            currentIndex: root.modelObject ? root.modelObject.sortOrder : 0
                            textRole: "text"
                            Accessible.name: qsTr("Sort")

                            onActivated: function(index) {
                                if (root.modelObject)
                                    root.modelObject.sortOrder = root.sortOptions[index].value
                            }

                            background: Rectangle {
                                radius: 12
                                color: ComponentTokens.toolbarControlBackground
                                border.width: sortCombo.activeFocus ? 1 : 0
                                border.color: ComponentTokens.inputBorderFocus
                            }

                            contentItem: Text {
                                leftPadding: 12
                                rightPadding: 26
                                text: sortCombo.displayText
                                color: SemanticTokens.textPrimary
                                font.pixelSize: SemanticTokens.fontSizeBody
                                font.family: SemanticTokens.fontFamily
                                font.weight: SemanticTokens.fontWeightMedium
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            indicator: ToolbarChevron {
                                x: sortCombo.width - width - 10
                                anchors.verticalCenter: parent.verticalCenter
                                strokeColor: SemanticTokens.textSecondary
                            }

                            popup: Popup {
                                y: sortCombo.height + 6
                                width: sortCombo.width
                                padding: 6

                                background: Rectangle {
                                    radius: 14
                                    color: ComponentTokens.toolbarPopupBackground
                                    border.width: 1
                                    border.color: ComponentTokens.toolbarPopupBorder
                                }

                                contentItem: ListView {
                                    implicitHeight: contentHeight
                                    model: sortCombo.popup.visible ? sortCombo.delegateModel : null
                                    clip: true
                                }
                            }

                            delegate: ItemDelegate {
                                required property var modelData
                                required property int index
                                width: ListView.view ? ListView.view.width : sortCombo.width
                                height: 36
                                highlighted: sortCombo.highlightedIndex === index

                                contentItem: Text {
                                    text: modelData.text
                                    color: SemanticTokens.textPrimary
                                    font.pixelSize: SemanticTokens.fontSizeBody
                                    font.family: SemanticTokens.fontFamily
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 10
                                }

                                background: Rectangle {
                                    radius: 10
                                    color: highlighted ? ComponentTokens.toolbarPopupHover : "transparent"
                                }
                            }
                        }

                        Rectangle {
                            width: headerRow.compactLayout ? 80 : 84
                            height: 40
                            radius: 14
                            color: ComponentTokens.toolbarControlBackground
                            border.width: 1
                            border.color: ComponentTokens.panelGlassBorder

                            Row {
                                anchors.fill: parent
                                anchors.margins: 2
                                spacing: 0

                                HistoryToggleButton {
                                    width: parent.width / 2
                                    height: parent.height
                                    selected: !root.listMode
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("Grid view")
                                    onClicked: root.listMode = false

                                    GridGlyph {
                                        anchors.centerIn: parent
                                        strokeColor: root.listMode ? SemanticTokens.textSecondary : SemanticTokens.textPrimary
                                    }
                                }

                                HistoryToggleButton {
                                    width: parent.width / 2
                                    height: parent.height
                                    selected: root.listMode
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("List view")
                                    onClicked: root.listMode = true

                                    ListGlyph {
                                        anchors.centerIn: parent
                                        strokeColor: root.listMode ? SemanticTokens.textPrimary : SemanticTokens.textSecondary
                                    }
                                }
                            }
                        }
                    }
                }

                // Projects keep the unmasked recording; say so where they are.
                Rectangle {
                    id: projectNote
                    objectName: "libraryProjectNote"
                    visible: root.showProjectNote && !root.confirming
                    width: parent.width
                    height: visible ? projectNoteText.implicitHeight + 20 : 0
                    radius: 12
                    color: ComponentTokens.toolbarControlBackground
                    border.width: 1
                    border.color: ComponentTokens.panelGlassBorder

                    Text {
                        id: projectNoteText
                        objectName: "libraryProjectNoteText"
                        x: 10
                        y: 10
                        width: parent.width - 20
                        wrapMode: Text.WordWrap
                        text: {
                            var parts = [qsTr("Projects keep the full recording, including anything you blurred or cropped out.")]
                            if (root.cleanupDays > 0)
                                parts.push(qsTr("Projects not changed for %1 days are removed automatically.").arg(root.cleanupDays))
                            if (root.recordings && !root.recordings.keepProjects)
                                parts.push(qsTr("Keeping new projects is off."))
                            return parts.join(" ")
                        }
                        color: SemanticTokens.textSecondary
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.family: SemanticTokens.fontFamily
                    }
                }

                // Delete asks first and says what goes and what stays.
                Rectangle {
                    id: confirmStrip
                    objectName: "libraryConfirmStrip"
                    readonly property int confirmKind: root.confirming && root.library
                        ? root.library.kindAt(root.library.indexOfKey(root.confirmKey)) : -1
                    visible: root.confirming
                    width: parent.width
                    height: visible ? Math.max(confirmText.implicitHeight, 34) + 20 : 0
                    radius: 12
                    color: Qt.rgba(0.94, 0.27, 0.27, SemanticTokens.isDarkMode ? 0.16 : 0.08)
                    border.width: 1
                    border.color: SemanticTokens.statusError
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: confirmText.text

                    Text {
                        id: confirmText
                        objectName: "libraryConfirmText"
                        anchors.left: parent.left
                        anchors.right: confirmButtons.left
                        anchors.margins: 10
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.WordWrap
                        text: confirmStrip.confirmKind === root.kindRecording
                            ? qsTr("Delete this editable project? Its full recording and edits are removed. The file you saved from it stays.")
                            : qsTr("Delete this screenshot from the Library? Files you saved from it stay.")
                        color: SemanticTokens.textPrimary
                        font.pixelSize: SemanticTokens.fontSizeBody
                        font.family: SemanticTokens.fontFamily
                    }

                    Row {
                        id: confirmButtons
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        HistoryActionButton {
                            id: confirmDeleteButton
                            objectName: "libraryConfirmDelete"
                            textLabel: qsTr("Delete")
                            danger: true
                            activeFocusOnTab: true
                            Keys.onReturnPressed: root.confirmDelete()
                            Keys.onEnterPressed: root.confirmDelete()
                            Keys.onSpacePressed: root.confirmDelete()
                            Keys.onEscapePressed: root.cancelDelete()
                            onClicked: root.confirmDelete()
                        }

                        HistoryActionButton {
                            id: confirmCancelButton
                            objectName: "libraryConfirmCancel"
                            textLabel: qsTr("Cancel")
                            activeFocusOnTab: true
                            Keys.onReturnPressed: root.cancelDelete()
                            Keys.onSpacePressed: root.cancelDelete()
                            Keys.onEscapePressed: root.cancelDelete()
                            onClicked: root.cancelDelete()
                        }
                    }
                }

                Item {
                    id: resultsArea
                    width: parent.width
                    height: parent.height - y

                    Loader {
                        anchors.fill: parent
                        sourceComponent: !root.hasVisibleResults
                            ? emptyResultsComponent
                            : root.listMode
                                ? listResultsComponent
                                : gridResultsComponent
                    }
                }
            }
        }
    }

    ContextActionMenu {
        id: contextMenu
        model: []

        onActionTriggered: function(actionId) {
            root.triggerAction(actionId)
        }
    }
}
