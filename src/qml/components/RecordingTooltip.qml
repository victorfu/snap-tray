import QtQuick
import SnapTrayQml

Item {
    id: root

    property string tooltipText: ""
    property color themeGlassBg: ComponentTokens.tooltipBackground
    property color themeGlassBgTop: ComponentTokens.tooltipBackgroundTop
    property color themeHighlight: ComponentTokens.tooltipHighlight
    property color themeBorder: ComponentTokens.tooltipBorder
    property color themeText: ComponentTokens.tooltipText
    property int themeCornerRadius: ComponentTokens.tooltipRadius

    readonly property int paddingH: 8
    readonly property int paddingV: 4

    implicitWidth: Math.ceil(textLabel.implicitWidth) + paddingH * 2
    implicitHeight: Math.ceil(textLabel.implicitHeight) + paddingV * 2
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: root.themeCornerRadius

        gradient: Gradient {
            GradientStop { position: 0.0; color: root.themeGlassBgTop }
            GradientStop { position: 1.0; color: root.themeGlassBg }
        }
    }

    Item {
        anchors.fill: parent
        clip: true

        Rectangle {
            width: parent.width
            height: parent.height / 2
            radius: root.themeCornerRadius

            gradient: Gradient {
                GradientStop { position: 0.0; color: root.themeHighlight }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.themeCornerRadius
        color: "transparent"
        border.width: 1
        border.color: root.themeBorder
    }

    Text {
        id: textLabel
        anchors.centerIn: parent
        text: root.tooltipText
        color: root.themeText
        font.pixelSize: 11
        font.family: PrimitiveTokens.fontFamily
        font.weight: Font.Medium
    }
}
