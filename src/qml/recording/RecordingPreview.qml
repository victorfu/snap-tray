import QtQuick
import QtQuick.Layouts
import SnapTrayQml

/**
 * RecordingPreview.qml
 *
 * Video preview window for reviewing screen recordings before saving.
 * Uses token-driven colors/timings and SVG icons with dynamic tint.
 */
Item {
    id: root

    readonly property color accent: SemanticTokens.accentDefault
    readonly property color accentHover: SemanticTokens.accentHover
    readonly property color iconColor: SemanticTokens.iconColor
    readonly property color textPrimary: SemanticTokens.textPrimary
    readonly property color textSecondary: SemanticTokens.textSecondary
    readonly property color bgPanel: ComponentTokens.recordingPreviewPanel
    readonly property color bgPanelHover: ComponentTokens.recordingPreviewPanelHover
    readonly property color bgPanelPressed: ComponentTokens.recordingPreviewPanelPressed
    readonly property color borderColor: ComponentTokens.recordingPreviewBorder
    readonly property color trimOverlayColor: ComponentTokens.recordingPreviewTrimOverlay

    property bool isScrubbing: false
    property bool isDraggingTrimStart: false
    property bool isDraggingTrimEnd: false

    readonly property var speedOptions: [0.25, 0.5, 0.75, 1.0, 1.5, 2.0]
    property int speedIndex: 3

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: ComponentTokens.recordingPreviewBgStart }
            GradientStop { position: 1.0; color: ComponentTokens.recordingPreviewBgEnd }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            VideoPlaybackItem {
                id: videoPlayer
                anchors.fill: parent
                source: backend.videoPath

                onDurationChanged: function(durationMs) { backend.updateDuration(durationMs) }
                onPositionChanged: function(positionMs) { backend.updatePosition(positionMs) }
                onStateChanged: backend.updatePlayingState(videoPlayer.playing)

                Component.onCompleted: {
                    setLooping(true)
                    play()
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: videoPlayer.togglePlayPause()
            }

            Rectangle {
                id: playOverlay
                anchors.centerIn: parent
                width: 72
                height: 72
                radius: 36
                color: ComponentTokens.recordingPreviewPlayOverlay
                visible: !videoPlayer.playing
                opacity: visible ? 1.0 : 0.0

                Behavior on opacity {
                    NumberAnimation { duration: PrimitiveTokens.durationInteractionHover }
                }

                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/play.svg"
                    iconSize: ComponentTokens.recordingPreviewActionIconSize
                    color: ComponentTokens.recordingPreviewPrimaryButtonIcon
                }
            }
        }

        Item {
            id: timelineArea
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.leftMargin: PrimitiveTokens.spacing16
            Layout.rightMargin: PrimitiveTokens.spacing16

            readonly property real trackY: 16
            readonly property real trackHeight: 6
            readonly property real handleWidth: 8
            readonly property real playheadRadius: 7
            readonly property real dur: videoPlayer.duration > 0 ? videoPlayer.duration : 1

            function posToX(ms) {
                return (ms / dur) * trackRect.width + trackRect.x
            }

            function xToPos(x) {
                var fraction = Math.max(0, Math.min(1, (x - trackRect.x) / trackRect.width))
                return Math.round(fraction * dur)
            }

            Rectangle {
                id: trackRect
                x: timelineArea.handleWidth
                y: timelineArea.trackY
                width: timelineArea.width - 2 * timelineArea.handleWidth
                height: timelineArea.trackHeight
                radius: 3
                color: root.bgPanel
                border.width: 1
                border.color: root.borderColor
            }

            Rectangle {
                x: trackRect.x
                y: trackRect.y
                width: Math.max(0, timelineArea.posToX(videoPlayer.position) - trackRect.x)
                height: trackRect.height
                radius: 3
                color: root.accent
            }

            Rectangle {
                visible: backend.hasTrim
                x: trackRect.x
                y: trackRect.y - 4
                width: Math.max(0, timelineArea.posToX(backend.trimStart) - trackRect.x)
                height: trackRect.height + 8
                color: root.trimOverlayColor
                radius: 2
            }

            Rectangle {
                visible: backend.hasTrim
                x: timelineArea.posToX(backend.trimEnd)
                y: trackRect.y - 4
                width: Math.max(0, trackRect.x + trackRect.width - timelineArea.posToX(backend.trimEnd))
                height: trackRect.height + 8
                color: root.trimOverlayColor
                radius: 2
            }

            Rectangle {
                id: trimStartHandle
                visible: backend.hasTrim
                x: timelineArea.posToX(backend.trimStart) - width / 2
                y: trackRect.y - 6
                width: timelineArea.handleWidth
                height: trackRect.height + 12
                radius: 2
                color: root.isDraggingTrimStart ? root.accentHover : root.accent
            }

            Rectangle {
                id: trimEndHandle
                visible: backend.hasTrim
                x: timelineArea.posToX(backend.trimEnd) - width / 2
                y: trackRect.y - 6
                width: timelineArea.handleWidth
                height: trackRect.height + 12
                radius: 2
                color: root.isDraggingTrimEnd ? root.accentHover : root.accent
            }

            Rectangle {
                id: playhead
                x: timelineArea.posToX(videoPlayer.position) - width / 2
                y: trackRect.y - (height - trackRect.height) / 2
                width: timelineArea.playheadRadius * 2
                height: timelineArea.playheadRadius * 2
                radius: timelineArea.playheadRadius
                color: ComponentTokens.recordingPreviewPlayhead
                border.width: 1
                border.color: ComponentTokens.recordingPreviewPlayheadBorder
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true

                property bool draggingTrimStart: false
                property bool draggingTrimEnd: false
                property bool wasPlayingBeforeScrub: false

                function resetInteractionState() {
                    var shouldResume = root.isScrubbing && wasPlayingBeforeScrub
                    draggingTrimStart = false
                    draggingTrimEnd = false
                    root.isDraggingTrimStart = false
                    root.isDraggingTrimEnd = false
                    root.isScrubbing = false
                    wasPlayingBeforeScrub = false
                    if (shouldResume) {
                        videoPlayer.play()
                    }
                }

                onPressed: function(mouse) {
                    var mx = mouse.x
                    wasPlayingBeforeScrub = false

                    if (backend.hasTrim) {
                        var tsX = timelineArea.posToX(backend.trimStart)
                        var teX = timelineArea.posToX(backend.trimEnd)

                        if (Math.abs(mx - tsX) < 12) {
                            draggingTrimStart = true
                            root.isDraggingTrimStart = true
                            return
                        }
                        if (Math.abs(mx - teX) < 12) {
                            draggingTrimEnd = true
                            root.isDraggingTrimEnd = true
                            return
                        }
                    }

                    root.isScrubbing = true
                    wasPlayingBeforeScrub = videoPlayer.playing
                    videoPlayer.pause()
                    videoPlayer.seek(timelineArea.xToPos(mx))
                }

                onPositionChanged: function(mouse) {
                    var mx = mouse.x
                    if (draggingTrimStart) {
                        var pos = timelineArea.xToPos(mx)
                        backend.trimStart = Math.max(0, Math.min(pos, backend.trimEnd - 100))
                    } else if (draggingTrimEnd) {
                        var pos2 = timelineArea.xToPos(mx)
                        backend.trimEnd = Math.max(backend.trimStart + 100, Math.min(pos2, videoPlayer.duration))
                    } else if (root.isScrubbing) {
                        videoPlayer.seek(timelineArea.xToPos(mx))
                    }
                }

                onReleased: {
                    resetInteractionState()
                }

                onCanceled: {
                    resetInteractionState()
                }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: timelineArea.handleWidth
                anchors.top: trackRect.bottom
                anchors.topMargin: 6
                text: backend.formatTime(videoPlayer.position)
                font.pixelSize: PrimitiveTokens.fontSizeSmall
                font.family: PrimitiveTokens.fontFamily
                color: root.textSecondary
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: timelineArea.handleWidth
                anchors.top: trackRect.bottom
                anchors.topMargin: 6
                text: backend.formatTime(videoPlayer.duration)
                font.pixelSize: PrimitiveTokens.fontSizeSmall
                font.family: PrimitiveTokens.fontFamily
                color: root.textSecondary
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: root.bgPanel

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: root.borderColor
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: PrimitiveTokens.spacing12
                anchors.rightMargin: PrimitiveTokens.spacing12
                spacing: PrimitiveTokens.spacing8

                IconButton {
                    iconSource: videoPlayer.playing ? "qrc:/icons/icons/pause.svg" : "qrc:/icons/icons/play.svg"
                    onClicked: videoPlayer.togglePlayPause()
                }

                Text {
                    text: backend.formatTime(videoPlayer.position) + " / " + backend.formatTime(videoPlayer.duration)
                    font.pixelSize: PrimitiveTokens.fontSizeCaption
                    font.family: PrimitiveTokens.fontFamily
                    font.weight: PrimitiveTokens.fontWeightMedium
                    color: root.textPrimary
                    Layout.leftMargin: PrimitiveTokens.spacing4
                }

                IconButton {
                    iconText: root.speedOptions[root.speedIndex] + "x"
                    onClicked: {
                        root.speedIndex = (root.speedIndex + 1) % root.speedOptions.length
                        videoPlayer.playbackRate = root.speedOptions[root.speedIndex]
                    }
                }

                Item { Layout.fillWidth: true }

                Row {
                    spacing: 1

                    SegmentButton {
                        text: "MP4"
                        selected: backend.selectedFormat === 0
                        isFirst: true
                        isLast: false
                        onClicked: backend.selectedFormat = 0
                    }
                    SegmentButton {
                        text: "GIF"
                        selected: backend.selectedFormat === 1
                        isFirst: false
                        isLast: false
                        onClicked: backend.selectedFormat = 1
                    }
                    SegmentButton {
                        text: "WebP"
                        selected: backend.selectedFormat === 2
                        isFirst: false
                        isLast: true
                        onClicked: backend.selectedFormat = 2
                    }
                }

                Item { Layout.fillWidth: true }

                IconButton {
                    iconSource: videoPlayer.muted ? "qrc:/icons/icons/volume-x.svg" : "qrc:/icons/icons/volume-2.svg"
                    onClicked: videoPlayer.muted = !videoPlayer.muted
                }

                IconButton {
                    iconSource: "qrc:/icons/icons/scissors.svg"
                    highlighted: backend.hasTrim
                    onClicked: backend.toggleTrim()
                }

                Rectangle {
                    width: 1
                    Layout.preferredHeight: 24
                    color: root.borderColor
                }

                IconButton {
                    iconSource: "qrc:/icons/icons/trash-2.svg"
                    destructive: true
                    onClicked: backend.discard()
                }

                IconButton {
                    iconSource: "qrc:/icons/icons/save.svg"
                    primary: true
                    onClicked: backend.save()
                }
            }
        }
    }

    Rectangle {
        id: processingOverlay
        anchors.fill: parent
        color: SemanticTokens.backgroundOverlay
        visible: backend.isProcessing

        Column {
            anchors.centerIn: parent
            spacing: PrimitiveTokens.spacing16

            Rectangle {
                width: 280
                height: 6
                radius: 3
                color: root.bgPanel
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    width: parent.width * (backend.processProgress / 100.0)
                    height: parent.height
                    radius: 3
                    color: root.accent

                    Behavior on width {
                        NumberAnimation { duration: PrimitiveTokens.durationInteractionHover }
                    }
                }
            }

            Text {
                text: backend.processStatus
                font.pixelSize: PrimitiveTokens.fontSizeBody
                font.family: PrimitiveTokens.fontFamily
                color: root.textPrimary
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: backend.processProgress + "%"
                font.pixelSize: PrimitiveTokens.fontSizeCaption
                font.family: PrimitiveTokens.fontFamily
                color: root.textSecondary
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    Rectangle {
        id: errorBanner
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: PrimitiveTokens.spacing16
        height: visible ? errorRow.implicitHeight + PrimitiveTokens.spacing16 : 0
        visible: backend.errorMessage !== ""
        radius: PrimitiveTokens.radiusSmall
        color: ComponentTokens.recordingPreviewDangerHover
        z: 10

        Row {
            id: errorRow
            anchors.centerIn: parent
            spacing: PrimitiveTokens.spacing8

            Text {
                text: backend.errorMessage
                font.pixelSize: PrimitiveTokens.fontSizeBody
                font.family: PrimitiveTokens.fontFamily
                color: SemanticTokens.statusError
                anchors.verticalCenter: parent.verticalCenter
            }

            IconButton {
                iconSource: "qrc:/icons/icons/close.svg"
                iconSize: ComponentTokens.iconSizeMenu
                width: 20
                height: 20
                anchors.verticalCenter: parent.verticalCenter
                onClicked: backend.clearError()
            }
        }
    }

    focus: true

    Keys.onPressed: function(event) {
        if (backend.isProcessing) {
            event.accepted = true
            return
        }

        switch (event.key) {
        case Qt.Key_Space:
            videoPlayer.togglePlayPause()
            event.accepted = true
            break
        case Qt.Key_Escape:
            backend.discard()
            event.accepted = true
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            backend.save()
            event.accepted = true
            break
        case Qt.Key_M:
            videoPlayer.muted = !videoPlayer.muted
            event.accepted = true
            break
        case Qt.Key_Left:
            videoPlayer.seek(Math.max(0, videoPlayer.position - 5000))
            event.accepted = true
            break
        case Qt.Key_Right:
            videoPlayer.seek(Math.min(videoPlayer.duration, videoPlayer.position + 5000))
            event.accepted = true
            break
        case Qt.Key_Comma:
            videoPlayer.pause()
            videoPlayer.seek(Math.max(0, videoPlayer.position - videoPlayer.frameIntervalMs))
            event.accepted = true
            break
        case Qt.Key_Period:
            videoPlayer.pause()
            videoPlayer.seek(Math.min(videoPlayer.duration, videoPlayer.position + videoPlayer.frameIntervalMs))
            event.accepted = true
            break
        case Qt.Key_S:
            if (event.modifiers & Qt.ControlModifier) {
                backend.save()
                event.accepted = true
            }
            break
        case Qt.Key_BracketLeft:
            if (root.speedIndex > 0) {
                root.speedIndex--
                videoPlayer.playbackRate = root.speedOptions[root.speedIndex]
            }
            event.accepted = true
            break
        case Qt.Key_BracketRight:
            if (root.speedIndex < root.speedOptions.length - 1) {
                root.speedIndex++
                videoPlayer.playbackRate = root.speedOptions[root.speedIndex]
            }
            event.accepted = true
            break
        }
    }

    component IconButton: Rectangle {
        property url iconSource: ""
        property string iconText: ""
        property bool highlighted: false
        property bool destructive: false
        property bool primary: false
        property int iconSize: ComponentTokens.recordingPreviewIconSize

        signal clicked()

        width: Math.max(ComponentTokens.recordingPreviewControlButtonSize,
                        iconSource !== "" ? ComponentTokens.recordingPreviewControlButtonSize : label.implicitWidth + 16)
        height: ComponentTokens.recordingPreviewControlButtonHeight
        radius: PrimitiveTokens.radiusSmall

        color: {
            if (primary)
                return mouseArea.pressed ? ComponentTokens.recordingPreviewPrimaryButtonPressed
                                         : mouseArea.containsMouse ? ComponentTokens.recordingPreviewPrimaryButtonHover
                                                                   : ComponentTokens.recordingPreviewPrimaryButton
            if (destructive)
                return mouseArea.pressed ? ComponentTokens.recordingPreviewDangerPressed
                                         : mouseArea.containsMouse ? ComponentTokens.recordingPreviewDangerHover
                                                                   : "transparent"
            if (mouseArea.pressed)
                return root.bgPanelPressed
            if (mouseArea.containsMouse)
                return root.bgPanelHover
            if (highlighted)
                return ComponentTokens.recordingPreviewControlHighlight
            return "transparent"
        }

        Behavior on color {
            ColorAnimation { duration: PrimitiveTokens.durationInteractionHover }
        }

        Text {
            id: label
            anchors.centerIn: parent
            visible: parent.iconSource === ""
            text: parent.iconText
            font.pixelSize: PrimitiveTokens.fontSizeCaption
            font.family: PrimitiveTokens.fontFamily
            font.weight: PrimitiveTokens.fontWeightMedium
            color: parent.primary ? ComponentTokens.recordingPreviewPrimaryButtonIcon
                                  : parent.highlighted ? root.accent : root.textPrimary
        }

        SvgIcon {
            anchors.centerIn: parent
            visible: parent.iconSource !== ""
            source: parent.iconSource
            iconSize: parent.iconSize
            color: parent.primary ? ComponentTokens.recordingPreviewPrimaryButtonIcon
                                  : parent.destructive ? SemanticTokens.statusError
                                                       : parent.highlighted ? root.accent : root.iconColor
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }

    component SegmentButton: Rectangle {
        property string text: ""
        property bool selected: false
        property bool isFirst: false
        property bool isLast: false

        signal clicked()

        width: fmtLabel.implicitWidth + 20
        height: 28
        radius: PrimitiveTokens.radiusSmall

        color: selected ? root.accent
             : fmtMouseArea.pressed ? root.bgPanelPressed
             : fmtMouseArea.containsMouse ? root.bgPanelHover
             : root.bgPanel

        border.width: selected ? 0 : 1
        border.color: root.borderColor

        Rectangle {
            visible: !isFirst
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: root.borderColor
        }

        Text {
            id: fmtLabel
            anchors.centerIn: parent
            text: parent.text
            font.pixelSize: PrimitiveTokens.fontSizeSmall
            font.family: PrimitiveTokens.fontFamily
            font.weight: selected ? PrimitiveTokens.fontWeightSemiBold : PrimitiveTokens.fontWeightRegular
            color: selected ? ComponentTokens.recordingPreviewPrimaryButtonIcon : root.textSecondary
        }

        MouseArea {
            id: fmtMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }
}
