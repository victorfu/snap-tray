import QtQuick
import SnapTrayQml

/**
 * GlassSurface: Reusable 3-layer frosted glass panel.
 *
 * Structure:
 *   1. Gradient background (top → bottom)
 *   2. Highlight overlay on top half (frosted shine)
 *   3. Hairline border
 *
 * Usage:
 *   GlassSurface {
 *       glassBg: ComponentTokens.recordingControlBarBg
 *       glassBgTop: ComponentTokens.recordingControlBarBgTop
 *       glassHighlight: ComponentTokens.recordingControlBarHighlight
 *       glassBorder: ComponentTokens.recordingControlBarBorder
 *       glassRadius: ComponentTokens.recordingControlBarRadius
 *   }
 */
Item {
    id: root

    property color glassBg: ComponentTokens.toolbarBackground
    property color glassBgTop: glassBg
    property color glassHighlight: Qt.rgba(1, 1, 1, 0.06)
    property color glassBorder: SemanticTokens.borderDefault
    property int glassRadius: ComponentTokens.toolbarRadius

    // Layer 1: Gradient background
    Rectangle {
        anchors.fill: parent
        radius: root.glassRadius

        gradient: Gradient {
            GradientStop { position: 0.0; color: root.glassBgTop }
            GradientStop { position: 1.0; color: root.glassBg }
        }
    }

    // Layer 2: Highlight overlay (top half)
    Item {
        anchors.fill: parent
        clip: true

        Rectangle {
            width: parent.width
            height: parent.height / 2
            radius: root.glassRadius

            gradient: Gradient {
                GradientStop { position: 0.0; color: root.glassHighlight }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    // Layer 3: Hairline border
    Rectangle {
        anchors.fill: parent
        radius: root.glassRadius
        color: "transparent"
        border.width: 1
        border.color: root.glassBorder
    }
}
