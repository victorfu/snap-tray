import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

// Beautify (#227): results first. Named styles and backgrounds come before the
// pixel controls, which sit under a collapsible Customize. Names and order
// follow the recording editor's Frame (Background, Padding, Corners, Shadow,
// Aspect ratio).
Item {
    id: root

    signal dragStarted()
    signal dragMoved(double deltaX, double deltaY)
    signal dragFinished()

    readonly property var backend: typeof beautifyBackend !== "undefined" ? beautifyBackend : null
    property int leftPadding: 0
    property int rightPadding: 0
    // Stays as left while the panel is reused in a session.
    property bool customizeExpanded: false
    // Naming a style to save (#227); Esc then cancels naming instead of closing.
    property bool namingStyle: false

    function startNamingStyle() {
        if (!root.backend || root.backend.customStylesFull)
            return
        styleNameInput.text = root.backend.suggestedCustomStyleName()
        root.namingStyle = true
        styleNameInput.forceActiveFocus()
        styleNameInput.selectAll()
    }

    function finishNamingStyle(save) {
        if (save && root.backend && styleNameInput.text.trim().length > 0)
            root.backend.saveCustomStyle(styleNameInput.text)
        root.namingStyle = false
    }

    readonly property int contentMargin: 16
    readonly property int styleCardHeight: 64
    readonly property int styleCardWidth: Math.floor((width - 2 * contentMargin - 3 * 8) / 4)
    readonly property int swatchSize: 28
    // How much of a style's padding and corners the small card drawing shows.
    readonly property real cardPaddingScale: 0.09
    readonly property real cardCornerScale: 0.25

    readonly property color backgroundStart: root.backend ? root.backend.primaryColor : "transparent"
    readonly property color backgroundEnd: root.backend && root.backend.secondaryColorVisible
        ? root.backend.secondaryColor : root.backgroundStart

    // The screen's available height, set by QmlBeautifyPanel (0 = no limit).
    // A panel taller than that scrolls instead of leaving the screen.
    property int maxHeight: 0
    readonly property int naturalHeight: contentColumn.implicitHeight + 2 * contentMargin

    width: 360
    height: maxHeight > 0 ? Math.min(naturalHeight, maxHeight) : naturalHeight

    PanelSurface {
        anchors.fill: parent
        radius: 14
    }

    Flickable {
        id: scroller
        objectName: "beautifyPanelScroller"
        anchors.fill: parent
        z: 1
        clip: true
        contentWidth: width
        contentHeight: root.naturalHeight
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: scroller.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

    Column {
        id: contentColumn
        x: root.contentMargin
        y: root.contentMargin
        width: root.width - 2 * root.contentMargin
        spacing: 10
        property int leftPadding: 0
        property int rightPadding: 0

        Item {
            id: header
            width: parent.width
            height: 28

            Item {
                id: titleArea
                anchors {
                    left: parent.left
                    top: parent.top
                    bottom: parent.bottom
                    right: closeButton.left
                    rightMargin: 8
                }
            }

            MouseArea {
                id: dragArea
                objectName: "beautifyPanelDragArea"
                anchors.fill: titleArea
                z: 0
                cursorShape: pressed ? CursorTokens.panelDragActive : CursorTokens.panelDragIdle

                property point pressStart

                onPressed: function(mouse) {
                    pressStart = Qt.point(mouse.x, mouse.y)
                    root.dragStarted()
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        var dx = mouse.x - pressStart.x
                        var dy = mouse.y - pressStart.y
                        root.dragMoved(dx, dy)
                    }
                }

                onReleased: root.dragFinished()
            }

            Text {
                anchors {
                    left: titleArea.left
                    verticalCenter: parent.verticalCenter
                }
                text: qsTr("Beautify")
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeH3
                font.family: SemanticTokens.fontFamily
                font.weight: SemanticTokens.fontWeightSemiBold
            }

            Rectangle {
                id: closeButton
                objectName: "beautifyPanelDismissButton"
                anchors {
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                }
                width: 28
                height: 28
                radius: 8
                color: closeMouse.pressed
                    ? ComponentTokens.buttonGhostBackgroundPressed
                    : closeMouse.containsMouse
                        ? ComponentTokens.buttonGhostBackgroundHover
                        : "transparent"

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Close")
                Accessible.onPressAction: if (root.backend) root.backend.requestClose()

                Behavior on color {
                    ColorAnimation { duration: SemanticTokens.durationFast }
                }

                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/close.svg"
                    iconSize: ComponentTokens.iconSizeMenu
                    color: SemanticTokens.iconColor
                }

                MouseArea {
                    id: closeMouse
                    objectName: "beautifyPanelDismissMouseArea"
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: CursorTokens.clickable
                    onClicked: if (root.backend) root.backend.requestClose()
                }
            }
        }

        Rectangle {
            id: previewFrame
            width: parent.width
            height: 204
            radius: 12
            color: ComponentTokens.beautifyPreviewPlaceholder
            border.width: 1
            border.color: ComponentTokens.beautifyPreviewFrame
            clip: true

            BeautifyPreviewItem {
                id: previewItem
                anchors.fill: parent
                anchors.margins: 1
                backend: root.backend
                opacity: 1.0

                Connections {
                    target: root.backend

                    function onPreviewRevisionChanged() {
                        previewRefresh.restart()
                    }
                }

                SequentialAnimation {
                    id: previewRefresh
                    NumberAnimation { target: previewItem; property: "opacity"; to: 0.82; duration: 70; easing.type: Easing.OutCubic }
                    NumberAnimation { target: previewItem; property: "opacity"; to: 1.0; duration: 130; easing.type: Easing.OutCubic }
                }
            }
        }

        // ── Style ──────────────────────────────────────────────
        Item {
            width: parent.width
            height: styleCaption.implicitHeight

            Text {
                id: styleCaption
                anchors.left: parent.left
                text: qsTr("Style").toUpperCase()
                color: ComponentTokens.settingsSectionText
                font.pixelSize: SemanticTokens.fontSizeCaption
                font.weight: Font.Medium
                font.family: SemanticTokens.fontFamily
                font.letterSpacing: SemanticTokens.letterSpacingWide
            }

            Text {
                objectName: "beautifyCustomStyleCaption"
                anchors.left: styleCaption.right
                anchors.leftMargin: SemanticTokens.spacing8
                anchors.right: saveStyleButton.left
                anchors.rightMargin: SemanticTokens.spacing8
                anchors.verticalCenter: styleCaption.verticalCenter
                elide: Text.ElideRight
                visible: root.backend ? root.backend.currentStyle < 0 && root.backend.currentCustomStyle < 0 : false
                text: qsTr("Custom")
                color: SemanticTokens.textSecondary
                font.pixelSize: SemanticTokens.fontSizeCaption
                font.family: SemanticTokens.fontFamily
            }

            // Saves the current look by name; hidden once it is a saved style.
            Text {
                id: saveStyleButton
                objectName: "beautifySaveStyleButton"
                anchors.right: parent.right
                anchors.verticalCenter: styleCaption.verticalCenter
                visible: root.backend ? root.backend.currentCustomStyle < 0 && !root.namingStyle : false
                readonly property bool full: root.backend ? root.backend.customStylesFull : false
                text: qsTr("Save style…")
                color: full ? SemanticTokens.textTertiary : SemanticTokens.accentDefault
                font.pixelSize: SemanticTokens.fontSizeCaption
                font.weight: Font.Medium
                font.family: SemanticTokens.fontFamily
                font.underline: saveStyleMouse.containsMouse && !full

                activeFocusOnTab: visible
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Save style…")
                Accessible.description: full ? saveStyleButton.fullText : ""
                Accessible.onPressAction: root.startNamingStyle()
                Keys.onSpacePressed: root.startNamingStyle()
                Keys.onReturnPressed: root.startNamingStyle()
                readonly property string fullText: root.backend
                    ? qsTr("You can keep up to %1 saved styles. Delete one to save another.").arg(root.backend.maxCustomStyles)
                    : ""

                FocusFrame { showFocus: saveStyleButton.activeFocus }

                MouseArea {
                    id: saveStyleMouse
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: saveStyleButton.full ? Qt.ArrowCursor : CursorTokens.clickable
                    onClicked: root.startNamingStyle()
                }

                ToolTip.visible: saveStyleMouse.containsMouse && full
                ToolTip.text: fullText
                ToolTip.delay: 300
            }
        }

        // Name for the style being saved.
        Row {
            objectName: "beautifyStyleNameRow"
            width: parent.width
            spacing: 8
            visible: root.namingStyle
            height: visible ? 30 : 0

            Rectangle {
                width: parent.width - saveNameButton.width - cancelNameButton.width - 2 * parent.spacing
                height: 30
                radius: ComponentTokens.inputRadius
                color: ComponentTokens.inputBackground
                border.width: 1
                border.color: styleNameInput.activeFocus ? ComponentTokens.inputBorderFocus : ComponentTokens.inputBorder

                TextInput {
                    id: styleNameInput
                    objectName: "beautifyStyleNameInput"
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: TextInput.AlignVCenter
                    maximumLength: 40
                    color: SemanticTokens.textPrimary
                    selectionColor: ComponentTokens.inputSelectionBackground
                    selectedTextColor: ComponentTokens.inputSelectionText
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    clip: true
                    selectByMouse: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("Style name")
                    Keys.onReturnPressed: root.finishNamingStyle(true)
                    Keys.onEnterPressed: root.finishNamingStyle(true)
                    Keys.onEscapePressed: root.finishNamingStyle(false)
                }
            }

            SettingsButton {
                id: saveNameButton
                objectName: "beautifyStyleNameSave"
                text: qsTr("Save")
                primary: true
                enabled: styleNameInput.text.trim().length > 0
                onClicked: root.finishNamingStyle(true)
            }

            SettingsButton {
                id: cancelNameButton
                text: qsTr("Cancel")
                onClicked: root.finishNamingStyle(false)
            }
        }

        Row {
            id: styleRow
            width: parent.width
            spacing: 8

            Repeater {
                model: root.backend ? root.backend.styleModel : []

                delegate: StyleCard {
                    required property int index
                    required property var modelData
                    objectName: "beautifyStyle" + index
                    width: root.styleCardWidth
                    name: modelData.name
                    accessibleName: qsTr("Style: %1").arg(modelData.name)
                    padding: modelData.padding
                    cornerRadius: modelData.cornerRadius
                    shadowEnabled: modelData.shadowEnabled
                    shadowBlur: modelData.shadowBlur
                    startColor: root.backgroundStart
                    endColor: root.backgroundEnd
                    chosen: root.backend ? root.backend.currentStyle === index : false
                    onActivated: if (root.backend) root.backend.applyStyle(index)
                }
            }
        }

        // Saved styles: the whole look, drawn on their own background.
        Flow {
            id: savedStyles
            objectName: "beautifySavedStyles"
            width: parent.width
            spacing: 8
            visible: savedRepeater.count > 0

            Repeater {
                id: savedRepeater
                model: root.backend ? root.backend.customStyleModel : []

                delegate: StyleCard {
                    required property int index
                    required property var modelData
                    objectName: "beautifySavedStyle" + index
                    width: root.styleCardWidth
                    name: modelData.name
                    accessibleName: qsTr("Saved style: %1").arg(modelData.name)
                    padding: modelData.padding
                    cornerRadius: modelData.cornerRadius
                    shadowEnabled: modelData.shadowEnabled
                    shadowBlur: modelData.shadowBlur
                    startColor: modelData.startColor
                    endColor: modelData.endColor
                    chosen: root.backend ? root.backend.currentCustomStyle === index : false
                    deletable: true
                    onActivated: if (root.backend) root.backend.applyCustomStyle(index)
                    onDeleteRequested: if (root.backend) root.backend.deleteCustomStyle(index)
                }
            }
        }

        // ── Background ─────────────────────────────────────────
        Text {
            text: qsTr("Background").toUpperCase()
            color: ComponentTokens.settingsSectionText
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.weight: Font.Medium
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingWide
        }

        Flow {
            width: parent.width
            spacing: 8

            Repeater {
                model: root.backend ? root.backend.presetModel : []

                delegate: Rectangle {
                    id: swatch
                    required property int index
                    required property var modelData
                    readonly property bool chosen: root.backend ? root.backend.currentPreset === index : false

                    objectName: "beautifyBackground" + index
                    width: root.swatchSize
                    height: root.swatchSize
                    radius: SemanticTokens.radiusSmall
                    border.width: chosen || presetMouse.containsMouse ? 2 : 1
                    border.color: chosen
                        ? SemanticTokens.accentDefault
                        : presetMouse.containsMouse
                            ? ComponentTokens.beautifyPresetHoverRing
                            : ComponentTokens.beautifyPreviewFrame

                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: swatch.modelData.startColor }
                        GradientStop { position: 1.0; color: swatch.modelData.endColor }
                    }

                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Background: %1").arg(modelData.name)
                    Accessible.checkable: true
                    Accessible.checked: chosen
                    Accessible.onPressAction: if (root.backend) root.backend.applyPreset(index)
                    Keys.onSpacePressed: if (root.backend) root.backend.applyPreset(index)
                    Keys.onReturnPressed: if (root.backend) root.backend.applyPreset(index)

                    FocusFrame {
                        showFocus: swatch.activeFocus
                        extraRadius: swatch.radius
                    }

                    MouseArea {
                        id: presetMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: CursorTokens.clickable
                        onClicked: if (root.backend) root.backend.applyPreset(swatch.index)
                    }

                    ToolTip.visible: presetMouse.containsMouse
                    ToolTip.text: modelData.name
                    ToolTip.delay: 500
                }
            }
        }

        // ── Customize ──────────────────────────────────────────
        Rectangle {
            id: customizeHeader
            objectName: "beautifyCustomizeToggle"
            width: parent.width
            height: 32
            radius: SemanticTokens.radiusSmall
            color: customizeMouse.pressed
                ? ComponentTokens.buttonGhostBackgroundPressed
                : customizeMouse.containsMouse
                    ? ComponentTokens.buttonGhostBackgroundHover
                    : "transparent"

            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Customize")
            Accessible.description: root.customizeExpanded
                ? qsTr("Hide padding, corners, shadow and other details")
                : qsTr("Show padding, corners, shadow and other details")
            Accessible.checkable: true
            Accessible.checked: root.customizeExpanded
            Accessible.onPressAction: root.customizeExpanded = !root.customizeExpanded
            Keys.onSpacePressed: root.customizeExpanded = !root.customizeExpanded
            Keys.onReturnPressed: root.customizeExpanded = !root.customizeExpanded

            Behavior on color {
                ColorAnimation { duration: SemanticTokens.durationFast }
            }

            SvgIcon {
                id: customizeChevron
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                source: root.customizeExpanded
                    ? "qrc:/icons/icons/chevron-down.svg"
                    : "qrc:/icons/icons/chevron-right.svg"
                iconSize: ComponentTokens.iconSizeMenu
                color: SemanticTokens.iconColor
            }

            Text {
                anchors.left: customizeChevron.right
                anchors.leftMargin: SemanticTokens.spacing8
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: qsTr("Customize")
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeBody
                font.weight: Font.Medium
                font.family: SemanticTokens.fontFamily
            }

            FocusFrame {
                showFocus: customizeHeader.activeFocus
                extraRadius: customizeHeader.radius
            }

            MouseArea {
                id: customizeMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: CursorTokens.clickable
                onClicked: root.customizeExpanded = !root.customizeExpanded
            }
        }

        Column {
            id: customizeContent
            objectName: "beautifyCustomizeContent"
            width: parent.width
            spacing: 4
            visible: root.customizeExpanded
            // A Column still reserves its children's height while hidden.
            height: visible ? implicitHeight : 0
            property int leftPadding: 0
            property int rightPadding: 0

            SettingsCombo {
                objectName: "beautifyBackgroundType"
                label: qsTr("Background type")
                model: root.backend ? root.backend.backgroundTypeModel : []
                currentIndex: root.backend ? root.backend.backgroundType : 0
                onActivated: function(index) {
                    if (root.backend)
                        root.backend.backgroundType = index
                }
            }

            SettingsRow {
                label: qsTr("Colors")

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Repeater {
                        model: [
                            { secondary: false, name: qsTr("First color") },
                            { secondary: true, name: qsTr("Second color") }
                        ]

                        delegate: Rectangle {
                            id: colorChip
                            required property var modelData
                            visible: !modelData.secondary
                                || (root.backend ? root.backend.secondaryColorVisible : false)
                            width: root.swatchSize
                            height: root.swatchSize
                            radius: 8
                            color: root.backend
                                ? (modelData.secondary ? root.backend.secondaryColor : root.backend.primaryColor)
                                : "transparent"
                            border.width: 1
                            border.color: ComponentTokens.beautifyPreviewFrame

                            function pick() {
                                if (!root.backend)
                                    return
                                if (modelData.secondary)
                                    root.backend.pickSecondaryColor()
                                else
                                    root.backend.pickPrimaryColor()
                            }

                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.name
                            Accessible.onPressAction: pick()
                            Keys.onSpacePressed: pick()
                            Keys.onReturnPressed: pick()

                            FocusFrame {
                                showFocus: colorChip.activeFocus
                                extraRadius: colorChip.radius
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: CursorTokens.clickable
                                onClicked: colorChip.pick()
                            }
                        }
                    }
                }
            }

            SettingsSlider {
                objectName: "beautifyPaddingSlider"
                label: qsTr("Padding")
                from: 16
                to: 200
                suffix: "px"
                value: root.backend ? root.backend.padding : 16
                onMoved: function(value) {
                    if (root.backend)
                        root.backend.padding = value
                }
            }

            SettingsSlider {
                objectName: "beautifyCornerSlider"
                label: qsTr("Corners")
                from: 0
                to: 40
                suffix: "px"
                value: root.backend ? root.backend.cornerRadius : 0
                onMoved: function(value) {
                    if (root.backend)
                        root.backend.cornerRadius = value
                }
            }

            SettingsToggle {
                objectName: "beautifyShadowToggle"
                label: qsTr("Shadow")
                checked: root.backend ? root.backend.shadowEnabled : false
                onToggled: function(checked) {
                    if (root.backend)
                        root.backend.shadowEnabled = checked
                }
            }

            SettingsSlider {
                objectName: "beautifyBlurSlider"
                label: qsTr("Shadow blur")
                from: 0
                to: 100
                suffix: "px"
                value: root.backend ? root.backend.shadowBlur : 0
                opacity: enabled ? 1.0 : 0.45
                enabled: root.backend ? root.backend.shadowEnabled : false
                onMoved: function(value) {
                    if (root.backend)
                        root.backend.shadowBlur = value
                }
            }

            SettingsCombo {
                objectName: "beautifyAspectRatio"
                label: qsTr("Aspect ratio")
                model: root.backend ? root.backend.aspectRatioModel : []
                currentIndex: root.backend ? root.backend.aspectRatio : 0
                onActivated: function(index) {
                    if (root.backend)
                        root.backend.aspectRatio = index
                }
            }
        }

        Row {
            width: parent.width
            spacing: 8

            SettingsButton {
                width: Math.floor((parent.width - 16) / 3)
                text: qsTr("Copy")
                onClicked: if (root.backend) root.backend.requestCopy()
            }

            SettingsButton {
                width: Math.floor((parent.width - 16) / 3)
                text: qsTr("Save")
                primary: true
                onClicked: if (root.backend) root.backend.requestSave()
            }

            SettingsButton {
                width: Math.floor((parent.width - 16) / 3)
                text: qsTr("Close")
                onClicked: if (root.backend) root.backend.requestClose()
            }
        }
    }
    }

    // A style preview card: a small drawing of padding, corners and shadow on
    // a background, its name below, a chosen ring, and for saved styles a
    // delete button (also the Delete key).
    component StyleCard: Item {
        id: card
        property string name
        property string accessibleName
        property int padding
        property int cornerRadius
        property bool shadowEnabled
        property int shadowBlur
        property color startColor
        property color endColor
        property bool chosen: false
        property bool deletable: false
        signal activated()
        signal deleteRequested()

        height: root.styleCardHeight
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: accessibleName
        Accessible.checkable: true
        Accessible.checked: chosen
        Accessible.onPressAction: card.activated()
        Keys.onSpacePressed: card.activated()
        Keys.onReturnPressed: card.activated()
        Keys.onDeletePressed: if (card.deletable) card.deleteRequested()

        Rectangle {
            id: cardFrame
            anchors.top: parent.top
            width: parent.width
            height: parent.height - cardLabel.implicitHeight - 4
            radius: SemanticTokens.radiusSmall
            color: "transparent"
            border.width: card.chosen ? 2 : 1
            border.color: card.chosen
                ? SemanticTokens.accentDefault
                : cardMouse.containsMouse ? ComponentTokens.beautifyPresetHoverRing : ComponentTokens.beautifyPreviewFrame

            Rectangle {
                id: cardBackground
                anchors.fill: parent
                anchors.margins: 3
                radius: SemanticTokens.radiusSmall - 2
                clip: true
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: card.startColor }
                    GradientStop { position: 1.0; color: card.endColor }
                }

                readonly property real inset: Math.max(2, card.padding * root.cardPaddingScale)

                Rectangle {
                    visible: card.shadowEnabled
                    x: inner.x + 1
                    y: inner.y + 2
                    width: inner.width
                    height: inner.height
                    radius: inner.radius
                    color: "#000000"
                    opacity: Math.min(0.45, 0.12 + card.shadowBlur / 200)
                }

                Rectangle {
                    id: inner
                    x: cardBackground.inset
                    y: cardBackground.inset
                    width: cardBackground.width - 2 * cardBackground.inset
                    height: cardBackground.height - 2 * cardBackground.inset
                    radius: card.cornerRadius * root.cardCornerScale
                    color: "#ffffff"

                    Column {
                        x: 4
                        y: 4
                        spacing: 2
                        Repeater {
                            model: 3
                            Rectangle {
                                required property int index
                                width: Math.max(0, inner.width * (0.7 - 0.15 * index) - 8)
                                height: 2
                                radius: 1
                                color: "#c8ccd4"
                            }
                        }
                    }
                }
            }

            FocusFrame {
                showFocus: card.activeFocus
                extraRadius: cardFrame.radius
            }
        }

        Text {
            id: cardLabel
            anchors.bottom: parent.bottom
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: card.name
            color: card.chosen ? SemanticTokens.textPrimary : SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.weight: card.chosen ? Font.Medium : Font.Normal
            font.family: SemanticTokens.fontFamily
        }

        MouseArea {
            id: cardMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: CursorTokens.clickable
            onClicked: card.activated()
        }

        Rectangle {
            id: deleteButton
            objectName: "beautifyStyleDelete"
            visible: card.deletable && (cardMouse.containsMouse || deleteMouse.containsMouse || card.activeFocus)
            anchors.right: cardFrame.right
            anchors.top: cardFrame.top
            anchors.margins: -5
            width: 18
            height: 18
            radius: 9
            color: deleteMouse.containsMouse ? SemanticTokens.statusError : ComponentTokens.beautifyPreviewPlaceholder
            border.width: 1
            border.color: ComponentTokens.beautifyPreviewFrame
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Delete style %1").arg(card.name)
            Accessible.onPressAction: card.deleteRequested()

            SvgIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/icons/close.svg"
                iconSize: 10
                color: deleteMouse.containsMouse ? "#ffffff" : SemanticTokens.iconColor
            }

            MouseArea {
                id: deleteMouse
                anchors.fill: parent
                anchors.margins: -3
                hoverEnabled: true
                cursorShape: CursorTokens.clickable
                onClicked: card.deleteRequested()
            }
        }

        ToolTip.visible: cardMouse.containsMouse && cardLabel.truncated
        ToolTip.text: name
        ToolTip.delay: 500
    }
}
