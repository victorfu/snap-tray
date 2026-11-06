import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

// Stable visual slots: the player and inspector are never recreated on navigation.
AccessibilityScope {
    id: root
    Accessible.role: Accessible.Grouping
    property bool inspectorVisible: true
    property bool inspectorOverlay: false
    property string inspectorTitle: ""
    property url inspectorIcon: ""
    // Scope under the title: "Whole recording" or the selected object's time range.
    property string inspectorSubtitle: ""
    // A clip or effect is shown: offer the route back to the whole-recording settings.
    property bool showProjectLink: false
    // Height kept clear below an overlaid inspector (playback controls and timeline).
    property real overlayBottomInset: 0
    property alias workspace: workspace
    property alias headerContent: headerContent
    property alias inspectorContent: inspectorContent
    readonly property int headerHeight: 52
    readonly property int inspectorWidth: Math.min(304, width - 32)
    signal projectRequested()
    signal inspectorCloseRequested()

    Rectangle {
        anchors.fill: parent
        color: ComponentTokens.editorSurface
    }
    Rectangle {
        width: parent.width
        height: root.headerHeight
        color: ComponentTokens.editorSurface
        Item {
            id: headerContent
            anchors.fill: parent
            anchors.leftMargin: SemanticTokens.spacing16
            anchors.rightMargin: SemanticTokens.spacing12
            anchors.topMargin: SemanticTokens.spacing8
            anchors.bottomMargin: SemanticTokens.spacing8
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: ComponentTokens.editorBorder }
    }
    Item {
        id: workspace
        objectName: "editorWorkspaceSlot"
        y: root.headerHeight
        width: parent.width - (root.inspectorVisible && !root.inspectorOverlay ? root.inspectorWidth : 0)
        height: parent.height - y
    }

    // An overlaid inspector dims what it floats over; a click outside it closes it.
    Rectangle {
        objectName: "editorInspectorScrim"
        visible: root.inspectorVisible && root.inspectorOverlay
        y: root.headerHeight
        width: parent.width
        height: inspector.height
        color: ComponentTokens.editorScrim
        z: 9
        MouseArea {
            anchors.fill: parent
            onClicked: root.inspectorCloseRequested()
        }
    }

    // Soft edge so an overlaid inspector reads as a floating sheet.
    Rectangle {
        visible: inspector.visible && root.inspectorOverlay
        x: inspector.x - width
        y: inspector.y
        width: 12
        height: inspector.height
        z: 10
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: SemanticTokens.shadowMediumColor }
        }
    }

    Rectangle {
        id: inspector
        objectName: "editorInspector"
        visible: root.inspectorVisible
        anchors.right: parent.right
        y: root.headerHeight
        width: root.inspectorWidth
        height: parent.height - y - (root.inspectorOverlay ? root.overlayBottomInset : 0)
        color: ComponentTokens.editorSurface
        z: 10
        Rectangle { width: 1; height: parent.height; color: ComponentTokens.editorBorder }
        Rectangle {
            visible: root.inspectorOverlay
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: ComponentTokens.editorBorder
        }
        // An overlay inspector blocks clicks through to the canvas.
        MouseArea { anchors.fill: parent }

        Item {
            id: inspectorHeader
            x: SemanticTokens.spacing16
            y: SemanticTokens.spacing12
            width: parent.width - 2 * SemanticTokens.spacing16
            height: Math.max(28, titleText.implicitHeight + (subtitleText.visible ? subtitleText.implicitHeight + 2 : 0))
            SvgIcon {
                id: titleIcon
                anchors.verticalCenter: parent.verticalCenter
                visible: root.inspectorIcon != ""
                source: root.inspectorIcon
                iconSize: 18
                color: SemanticTokens.textPrimary
            }
            Text {
                id: titleText
                objectName: "editorInspectorTitle"
                anchors.verticalCenter: parent.verticalCenter
                x: titleIcon.visible ? titleIcon.width + 8 : 0
                width: parent.width - x - (closeButton.visible ? closeButton.width + 8 : 0)
                text: root.inspectorTitle
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeBody
                font.weight: SemanticTokens.fontWeightSemiBold
                elide: Text.ElideRight
                anchors.verticalCenterOffset: subtitleText.visible ? -(subtitleText.implicitHeight + 2) / 2 : 0
            }
            Text {
                id: subtitleText
                objectName: "editorInspectorSubtitle"
                visible: text.length > 0
                anchors.top: titleText.bottom
                anchors.topMargin: 2
                x: titleText.x
                width: titleText.width
                text: root.inspectorSubtitle
                color: SemanticTokens.textSecondary
                font.pixelSize: SemanticTokens.fontSizeCaption
                elide: Text.ElideRight
            }
            Rectangle {
                id: closeButton
                objectName: "editorInspectorClose"
                visible: root.inspectorOverlay
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                radius: SemanticTokens.radiusSmall
                color: closeMouse.containsMouse ? ComponentTokens.editorControlHover : "transparent"
                activeFocusOnTab: visible
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Hide settings")
                Accessible.onPressAction: root.inspectorCloseRequested()
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) root.inspectorCloseRequested() }
                Keys.onSpacePressed: function(event) { if (!event.isAutoRepeat) root.inspectorCloseRequested() }
                FocusFrame { anchors.fill: parent; showFocus: closeButton.activeFocus }
                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/close.svg"
                    iconSize: 16
                    color: SemanticTokens.textSecondary
                }
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: CursorTokens.clickable
                    onClicked: root.inspectorCloseRequested()
                }
            }
        }
        Rectangle {
            y: inspectorHeader.y + inspectorHeader.height + SemanticTokens.spacing12
            x: 1
            width: parent.width - 1
            height: 1
            color: ComponentTokens.editorBorder
        }
        ScrollView {
            id: contentScroll
            x: 1
            y: inspectorHeader.y + inspectorHeader.height + SemanticTokens.spacing12 + 1
            width: parent.width - 1
            height: parent.height - y - (projectLink.visible ? projectLink.height + 1 : 0)
            clip: true
            contentWidth: availableWidth
            contentHeight: inspectorContent.childrenRect.height
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Item { id: inspectorContent; width: contentScroll.availableWidth }
        }
        Rectangle {
            visible: projectLink.visible
            anchors.bottom: projectLink.top
            x: 1
            width: parent.width - 1
            height: 1
            color: ComponentTokens.editorBorder
        }
        // The single route back to whole-recording settings (#218).
        Rectangle {
            id: projectLink
            objectName: "editorReturnToProject"
            signal clicked()
            onClicked: root.projectRequested()
            visible: root.showProjectLink
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.inspectorOverlay ? 1 : 0
            x: 1
            width: parent.width - 1
            height: 44
            color: linkMouse.containsMouse ? ComponentTokens.editorControlHover : "transparent"
            activeFocusOnTab: visible
            Accessible.role: Accessible.Link
            Accessible.name: linkText.text
            Accessible.onPressAction: clicked()
            Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked() }
            Keys.onSpacePressed: function(event) { if (!event.isAutoRepeat) clicked() }
            FocusFrame { anchors.fill: parent; showFocus: projectLink.activeFocus }
            Row {
                x: SemanticTokens.spacing12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                SvgIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/icons/chevron-left.svg"
                    iconSize: 16
                    color: SemanticTokens.textSecondary
                }
                Text {
                    id: linkText
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Return to Project settings")
                    color: SemanticTokens.textPrimary
                    font.pixelSize: SemanticTokens.fontSizeSmall
                }
            }
            MouseArea {
                id: linkMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: CursorTokens.clickable
                onClicked: projectLink.clicked()
            }
        }
    }
}
