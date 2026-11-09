import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * SettingsRow: Generic label + content row for settings forms.
 *
 * Places a label on the left with a default content slot on the right. The
 * label column is labelWidth wide, and widens for a single word that would
 * otherwise break mid-word (long German compounds), up to maxLabelFraction
 * of the row (#227).
 *
 * resetKey (#227): a SettingsBackend property with a per-setting reset. While
 * the value differs from its default, a reset button shows in the left gutter.
 */
Row {
    id: root

    property string label: ""
    property int labelWidth: 140
    property real maxLabelFraction: 0.45
    property string resetKey: ""
    // Brief accent highlight when search opens this row.
    property bool searchHighlight: false

    readonly property bool resettable: resetKey.length > 0
        && typeof settingsBackend !== "undefined" && settingsBackend !== null
        && settingsBackend.changedSettings.indexOf(resetKey) >= 0

    default property alias content: contentContainer.data

    function longestWordWidth() {
        var words = root.label.split(/\s+/)
        var widest = 0
        for (var i = 0; i < words.length; i++)
            widest = Math.max(widest, labelMetrics.advanceWidth(words[i]))
        return Math.ceil(widest)
    }

    readonly property int effectiveLabelWidth: Math.min(
        Math.max(root.labelWidth, longestWordWidth() + labelText.rightPadding + 1),
        Math.max(root.labelWidth, Math.floor(root.width * root.maxLabelFraction)))

    width: parent ? parent.width - parent.leftPadding - parent.rightPadding : 0
    height: Math.max(36, labelText.implicitHeight + SemanticTokens.spacing8)
    spacing: 0

    FontMetrics {
        id: labelMetrics
        font: labelText.font
    }

    Text {
        id: labelText
        text: root.label
        color: SemanticTokens.textPrimary
        font.pixelSize: SemanticTokens.fontSizeBody
        font.family: SemanticTokens.fontFamily
        font.letterSpacing: SemanticTokens.letterSpacingDefault
        width: root.effectiveLabelWidth
        rightPadding: SemanticTokens.spacing12
        // Break between words; a word longer than the column still breaks.
        wrapMode: Text.Wrap
        anchors.verticalCenter: parent.verticalCenter

        Rectangle {
            objectName: "settingsSearchHighlight"
            z: -1
            x: -SemanticTokens.spacing8
            y: -SemanticTokens.spacing4
            width: root.width + 2 * SemanticTokens.spacing8
            height: parent.height + 2 * SemanticTokens.spacing4
            radius: SemanticTokens.radiusSmall
            color: SemanticTokens.accentDefault
            opacity: root.searchHighlight ? 0.14 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: SemanticTokens.durationNormal } }
        }

        // In the gutter left of the label, so it never moves the row's content.
        Rectangle {
            id: resetButton
            objectName: "settingsResetButton"
            visible: root.resettable
            x: -width - 2
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 20
            radius: SemanticTokens.radiusSmall
            color: resetMouse.pressed
                ? ComponentTokens.buttonGhostBackgroundPressed
                : resetMouse.containsMouse ? ComponentTokens.buttonGhostBackgroundHover : "transparent"

            activeFocusOnTab: visible
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Reset %1 to default").arg(root.label)
            Accessible.onPressAction: settingsBackend.resetSetting(root.resetKey)
            Keys.onSpacePressed: settingsBackend.resetSetting(root.resetKey)
            Keys.onReturnPressed: settingsBackend.resetSetting(root.resetKey)

            SvgIcon {
                anchors.centerIn: parent
                // Loaded only while shown: most rows never need it.
                source: root.resettable ? "qrc:/icons/icons/reset.svg" : ""
                iconSize: 14
                color: SemanticTokens.accentDefault
            }

            FocusFrame {
                showFocus: resetButton.activeFocus
                extraRadius: resetButton.radius
            }

            MouseArea {
                id: resetMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: CursorTokens.clickable
                onClicked: settingsBackend.resetSetting(root.resetKey)
            }

            ToolTip.visible: resetMouse.containsMouse
            ToolTip.text: qsTr("Reset to default")
            ToolTip.delay: 400
        }
    }

    Item {
        id: contentContainer
        width: root.width - root.effectiveLabelWidth
        height: parent.height
    }
}
