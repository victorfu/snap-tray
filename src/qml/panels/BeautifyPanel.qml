import QtQuick
import QtQuick.Layouts
import SnapTrayQml

Item {
    id: root

    signal dragStarted()
    signal dragMoved(double deltaX, double deltaY)
    signal dragFinished()

    readonly property var backend: typeof beautifyBackend !== "undefined" ? beautifyBackend : null
    property int leftPadding: 0
    property int rightPadding: 0

    width: 340
    height: contentColumn.implicitHeight + 32

    PanelSurface {
        anchors.fill: parent
        radius: 14
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        z: 0
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
    }

    Column {
        id: contentColumn
        x: 16
        y: 16
        z: 1
        width: parent.width - 32
        spacing: 10
        property int leftPadding: 0
        property int rightPadding: 0

        Row {
            width: parent.width
            height: 28
            spacing: 8

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Beautify")
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeH3
                font.family: SemanticTokens.fontFamily
                font.weight: SemanticTokens.fontWeightSemiBold
            }

            Item {
                width: Math.max(0, parent.width - titleText.width - closeButton.width - 8)
                height: 1
            }

            Text {
                id: titleText
                visible: false
                text: qsTr("Beautify")
            }

            Rectangle {
                id: closeButton
                width: 28
                height: 28
                radius: 8
                color: closeMouse.pressed
                    ? ComponentTokens.buttonGhostBackgroundPressed
                    : closeMouse.containsMouse
                        ? ComponentTokens.buttonGhostBackgroundHover
                        : "transparent"

                Behavior on color {
                    ColorAnimation { duration: SemanticTokens.durationFast }
                }

                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/close.svg"
                    iconSize: ComponentTokens.iconSizeMenu
                    color: SemanticTokens.iconColor
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: CursorTokens.clickable
                    onClicked: if (root.backend) root.backend.requestClose()
                }
            }
        }

        Rectangle {
            id: previewFrame
            width: parent.width
            height: 204
            radius: 12
            color: ComponentTokens.beautifyPreviewPlaceholder
            border.width: 1
            border.color: ComponentTokens.beautifyPreviewFrame
            clip: true

            BeautifyPreviewItem {
                id: previewItem
                anchors.fill: parent
                anchors.margins: 1
                backend: root.backend
                opacity: 1.0

                Connections {
                    target: root.backend

                    function onPreviewRevisionChanged() {
                        previewRefresh.restart()
                    }
                }

                SequentialAnimation {
                    id: previewRefresh
                    NumberAnimation { target: previewItem; property: "opacity"; to: 0.82; duration: 70; easing.type: Easing.OutCubic }
                    NumberAnimation { target: previewItem; property: "opacity"; to: 1.0; duration: 130; easing.type: Easing.OutCubic }
                }
            }
        }

        Row {
            width: parent.width
            spacing: 6

            Repeater {
                model: root.backend ? root.backend.presetModel : []

                delegate: Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    border.width: presetMouse.containsMouse ? 2 : 1
                    border.color: presetMouse.containsMouse
                        ? ComponentTokens.beautifyPresetHoverRing
                        : ComponentTokens.beautifyPreviewFrame

                    gradient: Gradient {
                        GradientStop { position: 0.0; color: modelData.startColor }
                        GradientStop { position: 1.0; color: modelData.endColor }
                    }

                    MouseArea {
                        id: presetMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: CursorTokens.clickable
                        onClicked: if (root.backend) root.backend.applyPreset(index)
                    }
                }
            }
        }

        SettingsCombo {
            width: parent.width
            label: qsTr("Background:")
            model: root.backend ? root.backend.backgroundTypeModel : []
            currentIndex: root.backend ? root.backend.backgroundType : 0
            onActivated: function(index) {
                if (root.backend)
                    root.backend.backgroundType = index
            }
        }

        SettingsRow {
            width: parent.width
            label: qsTr("Colors:")

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Rectangle {
                    width: 28
                    height: 28
                    radius: 8
                    color: root.backend ? root.backend.primaryColor : "transparent"
                    border.width: 1
                    border.color: ComponentTokens.beautifyPreviewFrame

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: CursorTokens.clickable
                        onClicked: if (root.backend) root.backend.pickPrimaryColor()
                    }
                }

                Rectangle {
                    visible: root.backend ? root.backend.secondaryColorVisible : false
                    width: 28
                    height: 28
                    radius: 8
                    color: root.backend ? root.backend.secondaryColor : "transparent"
                    border.width: 1
                    border.color: ComponentTokens.beautifyPreviewFrame

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: CursorTokens.clickable
                        onClicked: if (root.backend) root.backend.pickSecondaryColor()
                    }
                }
            }
        }

        SettingsSlider {
            width: parent.width
            label: qsTr("Padding:")
            from: 16
            to: 200
            suffix: "px"
            value: root.backend ? root.backend.padding : 16
            onMoved: function(value) {
                if (root.backend)
                    root.backend.padding = value
            }
        }

        SettingsSlider {
            width: parent.width
            label: qsTr("Corners:")
            from: 0
            to: 40
            suffix: "px"
            value: root.backend ? root.backend.cornerRadius : 0
            onMoved: function(value) {
                if (root.backend)
                    root.backend.cornerRadius = value
            }
        }

        SettingsCombo {
            width: parent.width
            label: qsTr("Ratio:")
            model: root.backend ? root.backend.aspectRatioModel : []
            currentIndex: root.backend ? root.backend.aspectRatio : 0
            onActivated: function(index) {
                if (root.backend)
                    root.backend.aspectRatio = index
            }
        }

        SettingsRow {
            width: parent.width
            label: qsTr("Shadow")

            Rectangle {
                id: shadowToggle
                anchors.verticalCenter: parent.verticalCenter
                width: ComponentTokens.toggleWidth
                height: ComponentTokens.toggleHeight
                radius: height / 2
                color: root.backend && root.backend.shadowEnabled
                    ? ComponentTokens.toggleTrackOn
                    : ComponentTokens.toggleTrackOff

                Rectangle {
                    width: ComponentTokens.toggleKnobSize
                    height: ComponentTokens.toggleKnobSize
                    radius: ComponentTokens.toggleKnobRadius
                    color: ComponentTokens.toggleKnob
                    anchors.verticalCenter: parent.verticalCenter
                    x: root.backend && root.backend.shadowEnabled
                        ? shadowToggle.width - width - ComponentTokens.toggleKnobInset
                        : ComponentTokens.toggleKnobInset

                    Behavior on x {
                        NumberAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: CursorTokens.clickable
                    onClicked: {
                        if (root.backend)
                            root.backend.shadowEnabled = !root.backend.shadowEnabled
                    }
                }
            }
        }

        SettingsSlider {
            width: parent.width
            label: qsTr("  Blur:")
            from: 0
            to: 100
            suffix: "px"
            value: root.backend ? root.backend.shadowBlur : 0
            opacity: root.backend && root.backend.shadowEnabled ? 1.0 : 0.45
            enabled: root.backend && root.backend.shadowEnabled
            onMoved: function(value) {
                if (root.backend)
                    root.backend.shadowBlur = value
            }
        }

        Row {
            width: parent.width
            spacing: 8

            SettingsButton {
                width: Math.floor((parent.width - 16) / 3)
                text: qsTr("Copy")
                onClicked: if (root.backend) root.backend.requestCopy()
            }

            SettingsButton {
                width: Math.floor((parent.width - 16) / 3)
                text: qsTr("Save")
                primary: true
                onClicked: if (root.backend) root.backend.requestSave()
            }

            SettingsButton {
                width: Math.floor((parent.width - 16) / 3)
                text: qsTr("Close")
                onClicked: if (root.backend) root.backend.requestClose()
            }
        }
    }
}
