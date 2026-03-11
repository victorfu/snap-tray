import QtQuick
import SnapTrayQml

/**
 * PanelSurface: Elevated panel surface shared by Phase 4 utility panels.
 */
Item {
    id: root

    property int radius: ComponentTokens.panelRadius
    property color backgroundColor: ComponentTokens.panelGlassBackground
    property color backgroundTopColor: ComponentTokens.panelGlassBackgroundTop
    property color highlightColor: ComponentTokens.panelGlassHighlight
    property color borderColor: ComponentTokens.panelGlassBorder

    GlassSurface {
        anchors.fill: parent
        glassRadius: root.radius
        glassBg: root.backgroundColor
        glassBgTop: root.backgroundTopColor
        glassHighlight: root.highlightColor
        glassBorder: root.borderColor
    }
}
