import QtQuick
import SnapTrayQml

/**
 * SharedStyleDropdownHost: QWidget-hosted bridge for the shared style sections.
 *
 * Wraps ArrowStyleSection / LineStyleSection so widget surfaces can embed the
 * same QML button + popup pair in-place while keeping the control anchored to
 * the slot rect supplied by ToolOptionsPanel.
 */
Item {
    id: root
    property var viewModel: sharedStyleDropdownViewModel
    property string kind: "arrow"
    property bool expandUpward: false

    readonly property var loadedSection: sectionLoader.item
    readonly property int anchorOffsetX: loadedSection ? loadedSection.popupOverflowLeft : 0
    readonly property int anchorOffsetY: loadedSection ? loadedSection.anchorOffsetY : 0
    readonly property bool dropdownOpen: loadedSection ? loadedSection.dropdownOpen : false

    implicitWidth: (loadedSection ? loadedSection.width : 0) + anchorOffsetX
    implicitHeight: loadedSection ? loadedSection.height : 0
    width: implicitWidth
    height: implicitHeight

    signal menuOpened()
    signal menuClosed()

    function closeMenu() {
        if (loadedSection && loadedSection.closeMenu)
            loadedSection.closeMenu()
    }

    function openMenu() {
        if (loadedSection && loadedSection.openMenu)
            loadedSection.openMenu()
    }

    Loader {
        id: sectionLoader
        sourceComponent: root.kind === "line" ? lineSectionComponent : arrowSectionComponent
    }

    Component {
        id: arrowSectionComponent

        ArrowStyleSection {
            x: root.anchorOffsetX
            viewModel: root.viewModel
            expandUpward: root.expandUpward
            onMenuOpened: root.menuOpened()
            onMenuClosed: root.menuClosed()
        }
    }

    Component {
        id: lineSectionComponent

        LineStyleSection {
            x: root.anchorOffsetX
            viewModel: root.viewModel
            expandUpward: root.expandUpward
            onMenuOpened: root.menuOpened()
            onMenuClosed: root.menuClosed()
        }
    }
}
