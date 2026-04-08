import QtQuick
import SnapTrayQml

Item {
    id: root

    required property QtObject viewModel
    property var windowMaskRects: sliderPopup.visible
        ? [
            Qt.rect(0, 0, root.popupWidth, root.popupHeight),
            Qt.rect(0, root.popupHeight, radiusToggle.width, root.gapHeight),
            Qt.rect(0, root.popupHeight + root.gapHeight, mainRow.width, root.mainHeight)
        ]
        : [
            Qt.rect(0, root.popupHeight + root.gapHeight, mainRow.width, root.mainHeight)
        ]

    // Root includes popup space above the main panel.
    // Layout: [popup 28px] [gap 4px] [main 28px]
    // When popup is hidden the top area is transparent.
    readonly property int popupHeight: 28
    readonly property int gapHeight: 4
    readonly property int mainHeight: 28
    readonly property int popupWidth: 200
    readonly property rect attachmentAnchorRect: Qt.rect(
        0,
        root.popupHeight + root.gapHeight,
        mainRow.width,
        root.mainHeight)

    implicitWidth: Math.max(mainRow.implicitWidth, popupWidth)
    implicitHeight: popupHeight + gapHeight + mainHeight

    // Theme-adaptive colors (follows toolbar style)
    readonly property color iconColor: ComponentTokens.toolbarIcon
    readonly property color textColor: SemanticTokens.textPrimary
    readonly property color hoverBg: ComponentTokens.toolbarControlBackgroundHover
    // Accent colors for active states
    readonly property color activeBg: SemanticTokens.accentDefault
    readonly property color activeIconColor: SemanticTokens.textOnAccent

    // --- Main panel (bottom portion) ---
    GlassSurface {
        id: mainPanel
        x: 0; y: root.popupHeight + root.gapHeight
        width: mainRow.implicitWidth; height: root.mainHeight
        glassRadius: PrimitiveTokens.radiusSmall
    }

    Row {
        id: mainRow
        y: root.popupHeight + root.gapHeight
        height: root.mainHeight

        // Radius toggle button
        Item {
            id: radiusToggle
            width: 36; height: root.mainHeight

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: PrimitiveTokens.radiusSmall
                color: root.viewModel.radiusEnabled
                    ? root.activeBg
                    : radiusHover.hovered
                        ? root.hoverBg
                        : "transparent"
                Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }
            }

            SvgIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/icons/radius.svg"
                color: root.viewModel.radiusEnabled ? root.activeIconColor : root.iconColor
                opacity: root.viewModel.radiusEnabled ? 1.0 : 0.4
                Behavior on opacity { NumberAnimation { duration: SemanticTokens.durationFast } }
                Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }
            }

            HoverHandler { id: radiusHover }
            TapHandler { id: radiusTap; onTapped: root.viewModel.toggleRadius() }
        }

        // Ratio button
        Item {
            id: ratioButton
            width: root.viewModel.ratioLocked ? 95 : 36
            height: root.mainHeight
            Behavior on width { NumberAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.OutCubic } }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: PrimitiveTokens.radiusSmall
                color: root.viewModel.ratioLocked
                    ? root.activeBg
                    : ratioHover.hovered
                        ? root.hoverBg
                        : "transparent"
                Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }
            }

            Row {
                anchors.centerIn: parent
                spacing: 4

                SvgIcon {
                    source: root.viewModel.ratioLocked
                        ? "qrc:/icons/icons/ratio-lock.svg"
                        : "qrc:/icons/icons/ratio-free.svg"
                    color: root.viewModel.ratioLocked ? root.activeIconColor : root.iconColor
                    anchors.verticalCenter: parent.verticalCenter
                    Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }
                }

                Text {
                    visible: root.viewModel.ratioLocked
                    text: root.viewModel.ratioText
                    color: root.activeIconColor
                    font.pixelSize: PrimitiveTokens.fontSizeSmallBody
                    font.family: SemanticTokens.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            HoverHandler { id: ratioHover }
            TapHandler { id: ratioTap; onTapped: root.viewModel.toggleLock() }
        }
    }

    // --- Slider popup (top portion, overlapping gap + toggle for hover bridge) ---
    Item {
        id: sliderPopupArea
        x: 0
        y: 0
        width: root.popupWidth
        height: root.popupHeight + root.gapHeight + radiusToggle.height

        property bool anyHovered: radiusHover.hovered || areaHover.hovered || sliderDrag.active
        HoverHandler { id: areaHover }

        Item {
            id: sliderPopup
            visible: root.viewModel.radiusEnabled && sliderPopupArea.anyHovered
            opacity: visible ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.OutCubic } }

            width: root.popupWidth; height: root.popupHeight
            anchors.top: parent.top

            GlassSurface {
                anchors.fill: parent
                glassRadius: PrimitiveTokens.radiusSmall
            }

            Row {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                // Minus button
                Item {
                    width: 22; height: parent.height
                    Rectangle {
                        anchors.centerIn: parent
                        width: 22; height: 22
                        radius: PrimitiveTokens.radiusSmall
                        color: minusHover.hovered ? root.hoverBg : "transparent"
                        Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }
                        Text { anchors.centerIn: parent; text: "\u2212"; color: root.textColor; font.pixelSize: 14 }
                        HoverHandler { id: minusHover }
                        TapHandler { id: minusTap; onTapped: root.viewModel.decrementRadius() }
                    }
                }

                // Slider track
                Item {
                    id: sliderTrack
                    width: parent.width - 22 - 22 - 28 - 3 * 4
                    height: parent.height

                    Rectangle {
                        id: trackBg
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 6
                        radius: 3
                        color: ComponentTokens.sliderTrack
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: trackBg.width * (root.viewModel.currentRadius / root.viewModel.maxRadius)
                        height: 6
                        radius: 3
                        color: ComponentTokens.sliderFill
                    }

                    Rectangle {
                        id: sliderKnob
                        anchors.verticalCenter: parent.verticalCenter
                        x: trackBg.width * (root.viewModel.currentRadius / root.viewModel.maxRadius) - width / 2
                        width: 10; height: 10
                        radius: 5
                        color: ComponentTokens.sliderKnob
                        border.width: 2
                        border.color: ComponentTokens.sliderFill
                    }

                    DragHandler {
                        id: sliderDrag
                        target: null
                        yAxis.enabled: false
                        xAxis.enabled: true
                        onActiveChanged: {
                            if (active) updateFromMouse()
                        }
                        onCentroidChanged: {
                            if (active) updateFromMouse()
                        }
                        function updateFromMouse() {
                            let ratio = Math.max(0, Math.min(1, centroid.position.x / trackBg.width))
                            let newRadius = Math.round(ratio * root.viewModel.maxRadius)
                            if (newRadius !== root.viewModel.currentRadius) {
                                root.viewModel.currentRadius = newRadius
                            }
                        }
                    }

                    TapHandler {
                        onTapped: function(eventPoint) {
                            let localX = eventPoint.position.x
                            let ratio = Math.max(0, Math.min(1, localX / trackBg.width))
                            let newRadius = Math.round(ratio * root.viewModel.maxRadius)
                            root.viewModel.currentRadius = newRadius
                        }
                    }
                }

                // Plus button
                Item {
                    width: 22; height: parent.height
                    Rectangle {
                        anchors.centerIn: parent
                        width: 22; height: 22
                        radius: PrimitiveTokens.radiusSmall
                        color: plusHover.hovered ? root.hoverBg : "transparent"
                        Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }
                        Text { anchors.centerIn: parent; text: "+"; color: root.textColor; font.pixelSize: 14 }
                        HoverHandler { id: plusHover }
                        TapHandler { id: plusTap; onTapped: root.viewModel.incrementRadius() }
                    }
                }

                // Value display
                Text {
                    width: 28; height: parent.height
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: root.viewModel.currentRadius
                    color: root.textColor
                    font.pixelSize: PrimitiveTokens.fontSizeSmall
                    font.family: SemanticTokens.fontFamily
                }
            }
        }
    }
}
