import QtQuick
import SnapTrayQml

/**
 * FloatingToolbar: Glass-effect toolbar for PinWindow annotation mode.
 *
 * Driven by PinToolbarViewModel (injected as "viewModel" context property).
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
    property var viewModel: pinToolbarViewModel

    // ── Signals to C++ bridge ──
    signal buttonHovered(int buttonId, real anchorX, real anchorY,
                         real anchorW, real anchorH)
    signal buttonUnhovered()
    signal dragStarted()
    signal dragFinished()
    signal dragMoved(real deltaX, real deltaY)

    // ── Layout constants (match WindowedToolbar dimensions) ──
    readonly property int barHeight: 32
    readonly property int buttonWidth: 28
    readonly property int buttonHeight: 24
    readonly property int buttonSpacing: 2
    readonly property int separatorWidth: 8
    readonly property int margin: 8
    readonly property int separatorLineHeight: 16

    width: buttonRow.width + margin * 2
    height: barHeight

    // ── Glass background ──
    GlassSurface {
        anchors.fill: parent
        glassRadius: 8
    }

    // ── Drag area (below buttons) ──
    MouseArea {
        id: dragArea
        anchors.fill: parent
        z: 0
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor

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

    // ── Button row ──
    Row {
        id: buttonRow
        anchors {
            left: parent.left
            leftMargin: root.margin
            verticalCenter: parent.verticalCenter
        }
        spacing: root.buttonSpacing
        z: 1

        Repeater {
            model: root.viewModel.buttons

            Loader {
                id: delegateLoader

                // Unpack model data
                readonly property var buttonData: modelData
                readonly property int buttonId: buttonData.id
                readonly property bool isSeparatorBefore: buttonData.separatorBefore
                readonly property bool isOCR: buttonData.isOCR

                // Skip OCR button if not available
                active: !(isOCR && !root.viewModel.ocrAvailable)
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
                        isAction: delegateLoader.buttonData.isAction
                        isActive: root.viewModel.activeTool === delegateLoader.buttonId
                        isDisabled: {
                            if (delegateLoader.buttonData.isUndo)
                                return !root.viewModel.canUndo;
                            if (delegateLoader.buttonData.isRedo)
                                return !root.viewModel.canRedo;
                            if (delegateLoader.buttonData.isShare)
                                return root.viewModel.shareInProgress;
                            return false;
                        }
                        width: root.buttonWidth
                        height: root.buttonHeight

                        onClicked: function(id) {
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
    }
}
