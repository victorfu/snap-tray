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
            if (!root.hasViewModel
                    || !root.viewModel.showWidthSection
                    || wheel.angleDelta.y === 0) {
                wheel.accepted = false
                return
            }

            var delta = wheel.angleDelta.y > 0 ? 1 : -1
            root.viewModel.handleWidthChanged(root.viewModel.currentWidth + delta)
            wheel.accepted = true
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

        Row {
            visible: root.hasViewModel
                     && (root.viewModel.showWidthSection
                         || root.viewModel.showColorSection
                         || root.viewModel.showAutoBlurSection)
            spacing: {
                if (!root.hasViewModel)
                    return 0
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
                visible: root.hasViewModel && root.viewModel.showColorSection
                viewModel: root.viewModel
                anchors.verticalCenter: parent.verticalCenter
            }

            AutoBlurSection {
                visible: root.hasViewModel && root.viewModel.showAutoBlurSection
                viewModel: root.viewModel
                anchors.verticalCenter: parent.verticalCenter
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

    }
}
