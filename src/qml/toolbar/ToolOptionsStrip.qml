import QtQuick
import SnapTrayQml

/**
 * ToolOptionsStrip: Floating sub-toolbar showing tool-specific options.
 *
 * Wraps multiple option sections in a GlassSurface panel.
 * Sections are shown/hidden based on viewModel.show*Section properties.
 */
Item {
    id: root
    property var viewModel: typeof pinToolOptionsViewModel !== "undefined"
        ? pinToolOptionsViewModel
        : null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property int panelRightMargin: 6
    readonly property int targetLabelInset: 8
    readonly property int targetLabelPixelSize: 11
    Accessible.name: root.hasViewModel ? root.viewModel.targetLabel : ""
    property bool autoBlurHintActive: false
    // A control asks for a tooltip (Auto Blur, the eyedropper #305, Snap to text
    // #304). QmlFloatingSubToolbar draws it outside this window, anchored to the
    // control it finds by controlName (its objectName).
    signal controlTooltipRequested(string controlName, string text)
    signal controlTooltipDismissed(string controlName)

    // The Snap to text toggle sits 2px after the eyedropper, the spacing of
    // adjacent icon buttons (Shape's), so the two read as one group (#304).
    readonly property int markerTextSnapGap: 2
    readonly property color controlHoverBg: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)

    // Content sizing: expand to fit the row of sections
    implicitWidth: glassBg.width
    implicitHeight: Math.max(glassBg.height, contentHeight + 8)
    width: implicitWidth
    height: implicitHeight

    // Computed content width from visible sections
    property real contentWidth: {
        var w = 0
        var visibleCount = 0
        for (var i = 0; i < sectionRow.children.length; i++) {
            var child = sectionRow.children[i]
            if (!child.visible)
                continue

            var childWidth = Math.max(child.width, child.implicitWidth)
            if (childWidth <= 0)
                continue

            w += childWidth
            visibleCount += 1
        }
        if (visibleCount > 1)
            w += (visibleCount - 1) * sectionRow.spacing
        return w
    }

    property real contentHeight: {
        var h = 28
        for (var i = 0; i < sectionRow.children.length; i++) {
            var child = sectionRow.children[i]
            if (!child.visible)
                continue

            h = Math.max(h, Math.max(child.height, child.implicitHeight))
        }
        return h
    }

    GlassSurface {
        id: glassBg
        width: contentWidth + root.panelRightMargin
        height: 36  // 28px content + 8px padding
        glassRadius: 6
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton

        onWheel: function(wheel) {
            if (!root.hasViewModel) {
                wheel.accepted = false
                return
            }
            if (root.viewModel.mosaicActive) {
                wheel.accepted = true
                return
            }
            wheel.accepted = root.viewModel.handleWidthWheelDelta(wheel.angleDelta.y)
        }
    }

    Row {
        id: sectionRow
        anchors.left: glassBg.left
        anchors.leftMargin: 0
        anchors.top: glassBg.top
        anchors.topMargin: 4
        spacing: 8
        height: 28
        z: 1

        // Says what the strip edits: the selected annotation or the next new one (#224).
        Item {
            id: targetLabelItem
            objectName: "toolOptionsTargetLabel"
            visible: root.hasViewModel && root.viewModel.hasContent
            width: targetLabelText.implicitWidth + root.targetLabelInset
            height: 28
            anchors.verticalCenter: parent.verticalCenter

            Text {
                id: targetLabelText
                x: root.targetLabelInset
                anchors.verticalCenter: parent.verticalCenter
                text: root.hasViewModel ? root.viewModel.targetLabel : ""
                // Accent (not the icon-on-accent token) so it reads on light and dark glass.
                color: root.hasViewModel && root.viewModel.editingSelection
                       ? (SemanticTokens.isDarkMode ? DesignSystem.accentLight : DesignSystem.accentDefault)
                       : ComponentTokens.toolbarIcon
                font.pixelSize: root.targetLabelPixelSize
                font.weight: root.hasViewModel && root.viewModel.editingSelection
                             ? Font.DemiBold : Font.Normal
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        Row {
            visible: root.hasViewModel
                     && (root.viewModel.showWidthSection
                         || root.viewModel.showColorSection
                         || root.viewModel.showAutoBlurSection)
            spacing: {
                if (!root.hasViewModel)
                    return 0
                // Marker shows only colors, so this spacing falls between the
                // eyedropper and the toggle.
                if (markerTextSnapToggle.visible)
                    return root.markerTextSnapGap - colorPaletteSection.sectionPadding
                if (root.viewModel.showWidthSection && root.viewModel.showColorSection)
                    return -2
                if (root.viewModel.showWidthSection
                        && !root.viewModel.showColorSection
                        && root.viewModel.showAutoBlurSection)
                    return 1
                return 0
            }
            anchors.verticalCenter: parent.verticalCenter

            WidthSection {
                visible: root.hasViewModel && root.viewModel.showWidthSection
                viewModel: root.viewModel
                anchors.verticalCenter: parent.verticalCenter
            }

            ColorPaletteSection {
                id: colorPaletteSection
                visible: root.hasViewModel && root.viewModel.showColorSection
                viewModel: root.viewModel
                anchors.verticalCenter: parent.verticalCenter
                onTooltipRequested: function(itemName, text) {
                    root.controlTooltipRequested(itemName, text)
                }
                onTooltipDismissed: function(itemName) {
                    root.controlTooltipDismissed(itemName)
                }
            }

            // Smart marker "Snap to text" (#281, #304): a fixed-size icon toggle
            // styled like Shape's Filled toggle, so its width never follows the locale.
            // It sits right after the eyedropper (see markerTextSnapGap).
            Rectangle {
                id: markerTextSnapToggle
                objectName: "markerTextSnapToggle"
                readonly property bool checked: root.hasViewModel && root.viewModel.markerTextSnapEnabled
                readonly property string tooltipText:
                    qsTr("Snap to text: marker strokes align to detected text lines")
                visible: root.hasViewModel && root.viewModel.markerTextSnapAvailable
                         && root.viewModel.markerActive && !root.viewModel.editingSelection
                width: 22
                height: 22
                radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: checked ? DesignSystem.accentDefault
                     : markerTextSnapMouse.containsMouse ? root.controlHoverBg
                     : "transparent"
                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Snap to text")
                Accessible.description: tooltipText
                Accessible.checkable: true
                Accessible.checked: checked
                Accessible.onToggleAction: markerTextSnapToggle.toggle()
                Accessible.onPressAction: markerTextSnapToggle.toggle()

                function toggle() {
                    if (root.hasViewModel)
                        root.viewModel.markerTextSnapEnabled = !root.viewModel.markerTextSnapEnabled
                }

                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/marker-snap-text.svg"
                    iconSize: 14
                    color: markerTextSnapToggle.checked
                        ? ComponentTokens.toolbarIconActive
                        : ComponentTokens.toolbarIcon
                }

                MouseArea {
                    id: markerTextSnapMouse
                    anchors.fill: parent
                    cursorShape: CursorTokens.toolbarControl
                    hoverEnabled: true
                    onEntered: root.controlTooltipRequested(markerTextSnapToggle.objectName,
                                                            markerTextSnapToggle.tooltipText)
                    onExited: root.controlTooltipDismissed(markerTextSnapToggle.objectName)
                    onClicked: markerTextSnapToggle.toggle()
                }
            }

            AutoBlurSection {
                visible: root.hasViewModel && root.viewModel.showAutoBlurSection
                viewModel: root.viewModel
                hintActive: root.autoBlurHintActive
                anchors.verticalCenter: parent.verticalCenter
                onButtonHovered: root.controlTooltipRequested(
                    "autoBlurButton", root.hasViewModel ? root.viewModel.autoBlurHintText : "")
                onButtonHoverExited: root.controlTooltipDismissed("autoBlurButton")
            }
        }

        ArrowStyleSection {
            id: arrowStyleSection
            visible: root.hasViewModel && root.viewModel.showArrowStyleSection
            viewModel: root.viewModel
            anchors.verticalCenter: parent.verticalCenter
            onMenuOpened: lineStyleSection.closeMenu()
        }

        LineStyleSection {
            id: lineStyleSection
            visible: root.hasViewModel && root.viewModel.showLineStyleSection
            viewModel: root.viewModel
            anchors.verticalCenter: parent.verticalCenter
            onMenuOpened: arrowStyleSection.closeMenu()
        }

        TextFormattingSection {
            visible: root.hasViewModel && root.viewModel.showTextSection
            viewModel: root.viewModel
            anchors.verticalCenter: parent.verticalCenter
        }

        ShapeSection {
            visible: root.hasViewModel && root.viewModel.showShapeSection
            viewModel: root.viewModel
            anchors.verticalCenter: parent.verticalCenter
        }

        SizeSection {
            visible: root.hasViewModel && root.viewModel.showSizeSection
            viewModel: root.viewModel
            anchors.verticalCenter: parent.verticalCenter
        }

        // Drawing order of the selected annotation (#268).
        ArrangeSection {
            objectName: "toolOptionsArrangeSection"
            visible: root.hasViewModel && root.viewModel.editingSelection
            viewModel: root.viewModel
            anchors.verticalCenter: parent.verticalCenter
        }

    }
}
