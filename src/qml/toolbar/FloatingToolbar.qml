import QtQuick
import SnapTrayQml
import "ToolbarButtonState.js" as ButtonState

/**
 * FloatingToolbar: Glass-effect toolbar for PinWindow annotation mode.
 *
 * Driven by a toolbar view model injected as the root "viewModel" property.
 * Uses GlassSurface for background, ToolbarButton for each tool, and
 * emits drag/tooltip signals handled by QmlWindowedToolbar C++ bridge.
 *
 * Visual structure:
 *   GlassSurface {
 *     Row { Repeater { ToolbarButton } }
 *     MouseArea (drag)
 *   }
 */
Item {
    id: root
    required property var viewModel
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    property var displayButtons: root.hasViewModel ? root.viewModel.buttons : []
    property var overflowButtons: []
    property int constrainedWidth: -1
    property bool showDragHandle: true
    property int iconPalette: 0
    readonly property bool useCaptureOverlayIconPalette: root.iconPalette === 1
    property color iconNormalColor: root.useCaptureOverlayIconPalette
        ? SemanticTokens.captureOverlaySelectionBorder
        : ComponentTokens.toolbarIcon
    property color iconActionColor: root.useCaptureOverlayIconPalette
        ? SemanticTokens.captureOverlaySelectionBorder
        : (SemanticTokens.isDarkMode ? DesignSystem.accentLight : DesignSystem.accentDefault)
    property color iconCancelColor: root.useCaptureOverlayIconPalette
        ? SemanticTokens.captureOverlaySelectionBorder
        : (SemanticTokens.isDarkMode ? DesignSystem.red400 : DesignSystem.red500)
    property color iconActiveColor: root.useCaptureOverlayIconPalette
        ? SemanticTokens.captureOverlaySelectionBorder
        : ComponentTokens.toolbarIconActive

    // ── Signals to C++ bridge ──
    signal buttonHovered(int buttonId, real anchorX, real anchorY,
                         real anchorW, real anchorH)
    signal buttonUnhovered()
    signal dragStarted()
    signal dragFinished()
    signal dragMoved(real deltaX, real deltaY)
    signal overflowRequested(real anchorX, real anchorY, real anchorW, real anchorH)

    // ── Layout constants (match WindowedToolbar dimensions) ──
    readonly property int barHeight: 32
    readonly property int buttonWidth: 28
    readonly property int buttonHeight: 24
    readonly property int buttonSpacing: 2
    readonly property int separatorWidth: 8
    readonly property int margin: 8
    readonly property int separatorLineHeight: 16
    readonly property int dragHandleWidth: 16
    readonly property int contentSpacing: 6

    width: constrainedWidth > 0 ? constrainedWidth : contentRow.width + margin * 2
    height: barHeight

    // ── Glass background ──
    GlassSurface {
        anchors.fill: parent
        glassRadius: 8
    }

    Item {
        id: contentRow
        anchors {
            left: parent.left
            leftMargin: Math.min(root.margin, Math.max(0, (root.width - 1) / 4))
            verticalCenter: parent.verticalCenter
        }
        width: buttonRow.width + (root.showDragHandle ? root.dragHandleWidth + root.contentSpacing : 0)
        height: root.barHeight
        z: 1

        Item {
            id: dragHandle
            visible: root.showDragHandle
            objectName: "toolbarDragHandle"
            width: root.dragHandleWidth
            height: root.barHeight

            Column {
                anchors.centerIn: parent
                spacing: 3

                Repeater {
                    model: 4

                    Row {
                        spacing: 3

                        Repeater {
                            model: 2

                            Rectangle {
                                width: 2
                                height: 2
                                radius: 1
                                color: ComponentTokens.toolbarIcon
                                opacity: dragArea.pressed ? 0.85 : 0.45
                            }
                        }
                    }
                }
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
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
                onCanceled: root.dragFinished()
            }
        }

        // ── Button row ──
        Row {
            id: buttonRow
            x: root.showDragHandle ? root.dragHandleWidth + root.contentSpacing : 0
            spacing: root.buttonSpacing

            Repeater {
                model: root.displayButtons

                Loader {
                    id: delegateLoader

                    // Unpack model data
                    readonly property var buttonData: modelData
                    readonly property int buttonId: buttonData.id
                    readonly property bool isSeparatorBefore: buttonData.separatorBefore
                    readonly property bool isOCR: buttonData.isOCR

                    // Skip OCR button if not available
                    active: root.hasViewModel && !(isOCR && !root.viewModel.ocrAvailable)
                    visible: active

                    sourceComponent: Item {
                        implicitWidth: separatorItem.width + toolbarButton.width
                        implicitHeight: root.barHeight
                        width: implicitWidth
                        height: implicitHeight

                        // Separator before (if configured)
                        Item {
                            id: separatorItem
                            width: delegateLoader.isSeparatorBefore ? root.separatorWidth : 0
                            height: root.barHeight
                            visible: delegateLoader.isSeparatorBefore

                            // Vertical separator line
                            Rectangle {
                                anchors.centerIn: parent
                                width: 1
                                height: root.separatorLineHeight
                                color: ComponentTokens.toolbarSeparator
                            }
                        }

                        ToolbarButton {
                            id: toolbarButton
                            x: separatorItem.width
                            anchors.verticalCenter: parent.verticalCenter
                            buttonId: delegateLoader.buttonId
                            iconSource: delegateLoader.buttonData.iconSource
                            tooltipText: delegateLoader.buttonData.tooltip
                            isAction: delegateLoader.buttonData.isAction || false
                            isCancel: delegateLoader.buttonData.isCancel || false
                            isActive: root.hasViewModel
                                      && root.viewModel.activeTool === delegateLoader.buttonId
                            isDisabled: ButtonState.isDisabled(delegateLoader.buttonData, root.viewModel)
                            iconNormalColor: root.iconNormalColor
                            iconActionColor: root.iconActionColor
                            iconCancelColor: root.iconCancelColor
                            iconActiveColor: root.iconActiveColor
                            width: root.buttonWidth
                            height: root.buttonHeight

                            onClicked: function(id) {
                                if (root.hasViewModel)
                                    root.viewModel.handleButtonClicked(id)
                            }

                            onHovered: function(id, globalX, globalY, w, h) {
                                root.buttonHovered(id, globalX, globalY, w, h)
                            }

                            onUnhovered: root.buttonUnhovered()
                        }
                    }
                }
            }
            ToolbarButton {
                id: moreButton
                objectName: "toolbarMoreButton"
                visible: root.overflowButtons.length > 0
                width: root.showDragHandle ? root.buttonWidth
                    : Math.max(1, Math.min(root.buttonWidth, root.width - contentRow.x * 2))
                height: root.buttonHeight
                y: (root.barHeight - height) / 2
                textLabel: "\u22ef"
                tooltipText: qsTr("More")
                isActive: root.hasViewModel && root.overflowButtons.some(function(button) {
                    return button.id === root.viewModel.activeTool
                })
                onClicked: {
                    var anchor = mapToItem(root, 0, 0)
                    root.overflowRequested(anchor.x, anchor.y, width, height)
                }
            }
        }
    }
}
