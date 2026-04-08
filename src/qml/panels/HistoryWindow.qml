import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SnapTrayQml

Item {
    id: root

    readonly property var modelObject: typeof historyModel !== "undefined" ? historyModel : null
    readonly property var backend: typeof historyBackend !== "undefined" ? historyBackend : null

    property int contextIndex: -1
    property string selectedEntryId: ""
    property int selectedIndex: -1
    property bool listMode: false

    readonly property bool hasHistory: modelObject && modelObject.totalCount > 0
    readonly property bool hasVisibleResults: modelObject && modelObject.count > 0
    readonly property string currentFilterLabel: filterLabel(modelObject ? modelObject.activeFilter : 0)
    readonly property int responsiveSidebarWidth: Math.max(180, Math.min(244, Math.floor(mainRow.width * 0.23)))

    property var primaryFolders: [
        { text: qsTr("All Screenshots"), filterValue: 0 }
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

    focus: true

    function filterLabel(filterValue) {
        switch (filterValue) {
        case 1:
            return qsTr("Last 7 Days")
        case 2:
            return qsTr("Large Files")
        case 3:
            return qsTr("Replayable")
        case 0:
        default:
            return qsTr("All Screenshots")
        }
    }

    function setSelectedEntry(entryId, itemIndex) {
        selectedEntryId = entryId
        selectedIndex = itemIndex
        contextIndex = itemIndex
    }

    function syncSelection() {
        if (!modelObject) {
            selectedEntryId = ""
            selectedIndex = -1
            contextIndex = -1
            return
        }

        var nextIndex = selectedEntryId !== "" ? modelObject.indexOfId(selectedEntryId) : -1
        if (nextIndex < 0) {
            if (modelObject.count > 0) {
                nextIndex = 0
                selectedEntryId = modelObject.idAt(0)
            } else {
                selectedEntryId = ""
                selectedIndex = -1
                contextIndex = -1
                return
            }
        }

        selectedIndex = nextIndex
        contextIndex = nextIndex
    }

    function openContextMenu(globalX, globalY, entryId, itemIndex) {
        setSelectedEntry(entryId, itemIndex)
        var localPoint = root.mapFromGlobal(globalX, globalY)
        contextMenu.x = Math.min(localPoint.x, root.width - contextMenu.width - 8)
        contextMenu.y = Math.min(localPoint.y, root.height - contextMenu.height - 8)
        contextMenu.open()
    }

    function triggerAction(actionId) {
        if (!backend || selectedIndex < 0)
            return

        if (actionId === "edit")
            backend.edit(selectedIndex)
        else if (actionId === "pin")
            backend.pin(selectedIndex)
        else if (actionId === "copy")
            backend.copy(selectedIndex)
        else if (actionId === "save")
            backend.saveAs(selectedIndex)
        else if (actionId === "delete")
            backend.deleteEntry(selectedIndex)
    }

    Keys.onPressed: function(event) {
        if (!backend || selectedIndex < 0)
            return

        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            backend.edit(selectedIndex)
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            backend.deleteEntry(selectedIndex)
            event.accepted = true
        }
    }

    Connections {
        target: root.modelObject

        function onModelReset() {
            root.syncSelection()
        }

        function onCountChanged() {
            if (root.modelObject && root.modelObject.count === 0 && root.selectedEntryId === "") {
                root.selectedIndex = -1
                root.contextIndex = -1
            }
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
        border.width: enabledButton && (actionMouse.containsMouse || actionMouse.pressed) ? 1 : 0
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

        property string entryId: ""
        property int itemIndex: -1
        property url thumbnailUrl
        property string titleText: ""
        property string metaText: ""
        property var detailLines: []
        property bool selected: false

        signal selectedEntry(string entryId, int itemIndex)
        signal openRequested(string entryId, int itemIndex)
        signal contextMenuRequested(real globalX, real globalY, string entryId, int itemIndex)

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
                text: gridCard.titleText
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeH2
                font.family: SemanticTokens.fontFamily
                font.weight: SemanticTokens.fontWeightSemiBold
                elide: Text.ElideRight
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
                gridCard.selectedEntry(gridCard.entryId, gridCard.itemIndex)
                if (mouse.button === Qt.RightButton) {
                    var point = gridCard.mapToGlobal(mouse.x, mouse.y)
                    gridCard.contextMenuRequested(point.x, point.y, gridCard.entryId, gridCard.itemIndex)
                }
            }

            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton)
                    gridCard.openRequested(gridCard.entryId, gridCard.itemIndex)
            }
        }
    }

    component HistoryListRow: Rectangle {
        id: listRow

        property string entryId: ""
        property int itemIndex: -1
        property url thumbnailUrl
        property string titleText: ""
        property string metaText: ""
        property string tooltipText: ""
        property bool selected: false

        signal selectedEntry(string entryId, int itemIndex)
        signal openRequested(string entryId, int itemIndex)
        signal contextMenuRequested(real globalX, real globalY, string entryId, int itemIndex)

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

                Text {
                    text: listRow.titleText
                    color: SemanticTokens.textPrimary
                    font.pixelSize: SemanticTokens.fontSizeH2
                    font.family: SemanticTokens.fontFamily
                    font.weight: SemanticTokens.fontWeightSemiBold
                    elide: Text.ElideRight
                }

                Text {
                    text: listRow.metaText
                    color: SemanticTokens.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeBody
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
                listRow.selectedEntry(listRow.entryId, listRow.itemIndex)
                if (mouse.button === Qt.RightButton) {
                    var point = listRow.mapToGlobal(mouse.x, mouse.y)
                    listRow.contextMenuRequested(point.x, point.y, listRow.entryId, listRow.itemIndex)
                }
            }

            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton)
                    listRow.openRequested(listRow.entryId, listRow.itemIndex)
            }
        }
    }

    Component {
        id: emptyHistoryComponent

        Item {
            anchors.fill: parent

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 360)
                spacing: 10

                Text {
                    width: parent.width
                    text: qsTr("No history items yet")
                    color: SemanticTokens.textPrimary
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: SemanticTokens.fontSizeH1
                    font.family: SemanticTokens.fontFamily
                    font.weight: SemanticTokens.fontWeightSemiBold
                }

                Text {
                    width: parent.width
                    text: qsTr("Screenshots you capture in SnapTray will appear here.")
                    color: SemanticTokens.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                }
            }
        }
    }

    Component {
        id: emptyResultsComponent

        Item {
            anchors.fill: parent

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 340)
                spacing: 10

                Text {
                    width: parent.width
                    text: qsTr("No results")
                    color: SemanticTokens.textPrimary
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: SemanticTokens.fontSizeH1
                    font.family: SemanticTokens.fontFamily
                    font.weight: SemanticTokens.fontWeightSemiBold
                }

                Text {
                    width: parent.width
                    text: qsTr("Try a different smart folder.")
                    color: SemanticTokens.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                }
            }
        }
    }

    Component {
        id: gridResultsComponent

        GridView {
            id: gridView
            anchors.fill: parent
            clip: true
            model: root.modelObject
            currentIndex: root.selectedIndex
            cellWidth: width > 0 ? Math.max(280, Math.floor(width / Math.max(1, Math.floor(width / 320)))) : 320
            cellHeight: 334
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                width: gridView.cellWidth
                height: gridView.cellHeight

                HistoryGridCard {
                    anchors.fill: parent
                    anchors.margins: 9
                    entryId: model.id
                    itemIndex: index
                    thumbnailUrl: model.thumbnailUrl
                    titleText: model.displayTitle
                    metaText: model.relativeDateText + " \u00b7 " + model.fileSizeText + " \u00b7 " + model.resolutionText
                    detailLines: model.tooltipText ? model.tooltipText.split("\n") : []
                    selected: root.selectedEntryId === model.id

                    onSelectedEntry: function(entryId, itemIndex) {
                        root.setSelectedEntry(entryId, itemIndex)
                    }

                    onOpenRequested: function(entryId, itemIndex) {
                        root.setSelectedEntry(entryId, itemIndex)
                        if (root.backend)
                            root.backend.edit(itemIndex)
                    }

                    onContextMenuRequested: function(globalX, globalY, entryId, itemIndex) {
                        root.openContextMenu(globalX, globalY, entryId, itemIndex)
                    }
                }
            }
        }
    }

    Component {
        id: listResultsComponent

        ListView {
            id: listView
            anchors.fill: parent
            clip: true
            spacing: 12
            model: root.modelObject
            currentIndex: root.selectedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                width: listView.width
                height: 104

                HistoryListRow {
                    anchors.fill: parent
                    entryId: model.id
                    itemIndex: index
                    thumbnailUrl: model.thumbnailUrl
                    titleText: model.displayTitle
                    metaText: model.relativeDateText + " \u00b7 " + model.fileSizeText + " \u00b7 " + model.resolutionText
                    tooltipText: model.tooltipText
                    selected: root.selectedEntryId === model.id

                    onSelectedEntry: function(entryId, itemIndex) {
                        root.setSelectedEntry(entryId, itemIndex)
                    }

                    onOpenRequested: function(entryId, itemIndex) {
                        root.setSelectedEntry(entryId, itemIndex)
                        if (root.backend)
                            root.backend.edit(itemIndex)
                    }

                    onContextMenuRequested: function(globalX, globalY, entryId, itemIndex) {
                        root.openContextMenu(globalX, globalY, entryId, itemIndex)
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
        anchors.margins: 12
        radius: 20
    }

    Row {
        id: mainRow
        anchors.fill: shell
        anchors.margins: 18
        spacing: 18

        Rectangle {
            id: sidebar
            width: root.responsiveSidebarWidth
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
                    spacing: 8

                    Repeater {
                        model: root.primaryFolders

                        delegate: HistorySidebarButton {
                            width: parent.width
                            textLabel: modelData.text
                            countLabel: root.modelObject
                                ? (root.modelObject.filterCountsRevision,
                                   String(root.modelObject.countForFilter(modelData.filterValue)))
                                : "0"
                            active: root.modelObject ? root.modelObject.activeFilter === modelData.filterValue : false
                            onClicked: {
                                if (!root.modelObject)
                                    return
                                root.modelObject.activeFilter = modelData.filterValue
                            }
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
                            width: parent.width
                            textLabel: modelData.text
                            countLabel: root.modelObject
                                ? (root.modelObject.filterCountsRevision,
                                   String(root.modelObject.countForFilter(modelData.filterValue)))
                                : "0"
                            active: root.modelObject ? root.modelObject.activeFilter === modelData.filterValue : false
                            onClicked: {
                                if (!root.modelObject)
                                    return
                                root.modelObject.activeFilter = modelData.filterValue
                            }
                        }
                    }
                }

                Item {
                    width: 1
                    height: 1
                }
            }
        }

        Rectangle {
            id: verticalDivider
            width: 1
            height: parent.height
            color: SemanticTokens.borderDefault
            opacity: 0.45
        }

        Item {
            id: contentArea
            width: parent.width - sidebar.width - verticalDivider.width - mainRow.spacing * 2
            height: parent.height

            Column {
                anchors.fill: parent
                spacing: 18

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

                        HistoryActionButton {
                            textLabel: qsTr("Edit")
                            iconSource: "qrc:/icons/icons/pencil.svg"
                            enabledButton: root.selectedIndex >= 0
                            onClicked: root.triggerAction("edit")
                        }

                        HistoryActionButton {
                            textLabel: qsTr("Pin")
                            iconSource: "qrc:/icons/icons/pin.svg"
                            enabledButton: root.selectedIndex >= 0
                            onClicked: root.triggerAction("pin")
                        }

                        HistoryActionButton {
                            textLabel: qsTr("Copy")
                            iconSource: "qrc:/icons/icons/copy.svg"
                            enabledButton: root.selectedIndex >= 0
                            onClicked: root.triggerAction("copy")
                        }

                        HistoryActionButton {
                            textLabel: qsTr("Save As")
                            iconSource: "qrc:/icons/icons/save.svg"
                            enabledButton: root.selectedIndex >= 0
                            onClicked: root.triggerAction("save")
                        }

                        HistoryActionButton {
                            textLabel: qsTr("Delete")
                            iconSource: "qrc:/icons/icons/trash-2.svg"
                            enabledButton: root.selectedIndex >= 0
                            danger: true
                            onClicked: root.triggerAction("delete")
                        }
                    }
                }

                Item {
                    id: headerRow
                    width: parent.width
                    readonly property bool compactLayout: width < 620
                    height: compactLayout ? 96 : 52

                    Column {
                        anchors.left: parent.left
                        y: headerRow.compactLayout ? 0 : Math.max(0, (headerRow.height - height) / 2)
                        spacing: 4

                        Text {
                            text: qsTr("History")
                            color: SemanticTokens.textTertiary
                            font.pixelSize: SemanticTokens.fontSizeCaption
                            font.family: SemanticTokens.fontFamily
                            font.weight: SemanticTokens.fontWeightSemiBold
                            font.letterSpacing: SemanticTokens.letterSpacingWide
                        }

                        Text {
                            text: root.currentFilterLabel
                            color: SemanticTokens.textPrimary
                            font.pixelSize: 34
                            font.family: SemanticTokens.fontFamily
                            font.weight: SemanticTokens.fontWeightSemiBold
                        }
                    }

                    Row {
                        id: headerControls
                        anchors.right: parent.right
                        y: headerRow.compactLayout ? headerRow.height - height : Math.max(0, (headerRow.height - height) / 2)
                        spacing: headerRow.compactLayout ? 8 : 10

                        SettingsButton {
                            height: 40
                            radius: 14
                            text: qsTr("Open History Folder")
                            onClicked: if (root.backend) root.backend.openHistoryFolder()
                        }

                        ComboBox {
                            id: sortCombo
                            width: headerRow.compactLayout ? 138 : 156
                            height: 40
                            model: root.sortOptions
                            currentIndex: root.modelObject ? root.modelObject.sortOrder : 0
                            textRole: "text"

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

                Item {
                    id: resultsArea
                    width: parent.width
                    height: parent.height - commandBar.height - headerRow.height - 36

                    Loader {
                        anchors.fill: parent
                        sourceComponent: !root.hasHistory
                            ? emptyHistoryComponent
                            : !root.hasVisibleResults
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
        model: [
            { id: "edit", text: qsTr("Edit"), icon: "qrc:/icons/icons/pencil.svg" },
            { id: "pin", text: qsTr("Pin"), icon: "qrc:/icons/icons/pin.svg" },
            { id: "copy", text: qsTr("Copy"), icon: "qrc:/icons/icons/copy.svg" },
            { id: "save", text: qsTr("Save As"), icon: "qrc:/icons/icons/save.svg" },
            { separator: true },
            { id: "delete", text: qsTr("Delete"), icon: "qrc:/icons/icons/trash-2.svg" }
        ]

        onActionTriggered: function(actionId) {
            root.triggerAction(actionId)
        }
    }
}
