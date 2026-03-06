import QtQuick
import QtQuick.Layouts
import SnapTrayQml

/**
 * RecordingControlBar: Floating control bar shown during screen recording.
 *
 * Features:
 *   - Glass-effect background (gradient + border + highlight)
 *   - Animated recording indicator (conical gradient / paused / preparing)
 *   - Duration, region size, FPS labels
 *   - Pause/Resume, Stop, Cancel buttons with hover states
 *   - Draggable window repositioning
 *
 * Properties set from C++ (QmlRecordingControlBar):
 *   duration, regionSize, fpsText, isPaused, isPreparing,
 *   preparingStatus, audioEnabled
 *
 * Theme colors set from C++ via context properties:
 *   themeGlassBg, themeGlassBgTop, themeHighlight, themeBorder,
 *   themeText, themeSeparator, themeHoverBg,
 *   themeIconNormal, themeIconActive, themeIconRecord, themeIconCancel,
 *   themeCornerRadius
 */
Item {
    id: root

    // ── Properties from C++ backend ──
    property string duration: "00:00:00"
    property string regionSize: "--"
    property string fpsText: qsTr("-- fps")
    property bool isPaused: false
    property bool isPreparing: false
    property string preparingStatus: ""
    property bool audioEnabled: false

    // ── Theme colors from C++ (ToolbarStyleConfig) ──
    property color themeGlassBg: Qt.rgba(0, 0, 0, 0.6)
    property color themeGlassBgTop: Qt.rgba(0, 0, 0, 0.68)
    property color themeHighlight: Qt.rgba(1, 1, 1, 0.06)
    property color themeBorder: Qt.rgba(1, 1, 1, 0.12)
    property color themeText: "#FFFFFF"
    property color themeSeparator: Qt.rgba(1, 1, 1, 0.2)
    property color themeHoverBg: Qt.rgba(1, 1, 1, 0.12)
    property color themeIconNormal: "#CCCCCC"
    property color themeIconActive: "#FFFFFF"
    property color themeIconRecord: "#FF3B30"
    property color themeIconCancel: "#FF453A"
    property int themeCornerRadius: 10

    // ── Signals to C++ backend ──
    signal stopRequested()
    signal cancelRequested()
    signal pauseRequested()
    signal resumeRequested()
    signal buttonHovered(int buttonId, real anchorX, real anchorY,
                         real anchorW, real anchorH)
    signal buttonUnhovered()
    signal dragStarted()
    signal dragFinished()
    signal dragMoved(real deltaX, real deltaY)
    signal contentWidthChanged()

    // ── Layout constants (matching Widget version exactly) ──
    readonly property int barHeight: 32
    readonly property int buttonSize: 24       // BUTTON_WIDTH(28) - 4
    readonly property int buttonSpacing: 2
    readonly property int contentMarginH: 10
    readonly property int contentMarginV: 4
    readonly property int itemSpacing: 6

    // ── Monospace font ──
    readonly property string monoFont: Qt.platform.os === "osx"
        ? "SF Mono,Menlo,Monaco,monospace" : "Consolas,Courier New,monospace"

    width: contentRow.implicitWidth + contentMarginH * 2
           + buttonArea.width + itemSpacing
    height: barHeight

    onWidthChanged: contentWidthChanged()

    // ========================================================================
    // Glass background layer 1: Gradient background
    // ========================================================================
    Rectangle {
        id: glassBg
        anchors.fill: parent
        radius: root.themeCornerRadius

        gradient: Gradient {
            GradientStop { position: 0.0; color: root.themeGlassBgTop }
            GradientStop { position: 1.0; color: root.themeGlassBg }
        }
    }

    // ========================================================================
    // Glass background layer 2: Top highlight
    // ========================================================================
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

    // ========================================================================
    // Glass background layer 3: Hairline border
    // ========================================================================
    Rectangle {
        anchors.fill: parent
        radius: root.themeCornerRadius
        color: "transparent"
        border.width: 1
        border.color: root.themeBorder
    }

    // ========================================================================
    // Drag area (background, below buttons)
    // ========================================================================
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

    // ========================================================================
    // Content row (left side: indicator + labels)
    // ========================================================================
    RowLayout {
        id: contentRow
        anchors {
            left: parent.left
            leftMargin: root.contentMarginH
            verticalCenter: parent.verticalCenter
        }
        spacing: root.itemSpacing
        z: 1

        // ── Recording indicator (12x12 animated circle) ──
        Canvas {
            id: indicatorCanvas
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            renderStrategy: Canvas.Threaded

            property real gradientAngle: 0.0

            NumberAnimation on gradientAngle {
                from: 0
                to: 1
                duration: ComponentTokens.recordingIndicatorDuration
                loops: Animation.Infinite
                running: !root.isPaused
            }

            onGradientAngleChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)

                if (root.isPreparing) {
                    // Gray with sine pulse alpha: 128 ± 64
                    var alpha = (128 + 64 * Math.sin(gradientAngle * 2 * Math.PI)) / 255
                    ctx.fillStyle = Qt.rgba(180/255, 180/255, 180/255, alpha)
                    ctx.beginPath()
                    ctx.arc(6, 6, 6, 0, 2 * Math.PI)
                    ctx.fill()
                } else if (root.isPaused) {
                    // Solid amber
                    ctx.fillStyle = ComponentTokens.recordingBoundaryPaused
                    ctx.beginPath()
                    ctx.arc(6, 6, 6, 0, 2 * Math.PI)
                    ctx.fill()
                } else {
                    // Conical gradient: blue → indigo → purple → blue
                    // Draw 48 segments to approximate conical gradient
                    var segments = 48
                    var angleOffset = gradientAngle * 2 * Math.PI
                    for (var i = 0; i < segments; i++) {
                        var t = i / segments
                        var r, g, b
                        if (t < 0.33) {
                            var p = t / 0.33
                            r = (0 + (88 - 0) * p) / 255
                            g = (122 + (86 - 122) * p) / 255
                            b = (255 + (214 - 255) * p) / 255
                        } else if (t < 0.66) {
                            var p2 = (t - 0.33) / 0.33
                            r = (88 + (175 - 88) * p2) / 255
                            g = (86 + (82 - 86) * p2) / 255
                            b = (214 + (222 - 214) * p2) / 255
                        } else {
                            var p3 = (t - 0.66) / 0.34
                            r = (175 + (0 - 175) * p3) / 255
                            g = (82 + (122 - 82) * p3) / 255
                            b = (222 + (255 - 222) * p3) / 255
                        }

                        var startAngle = angleOffset + (i / segments) * 2 * Math.PI
                        var endAngle = angleOffset + ((i + 1) / segments) * 2 * Math.PI

                        ctx.fillStyle = Qt.rgba(r, g, b, 1.0)
                        ctx.beginPath()
                        ctx.moveTo(6, 6)
                        ctx.arc(6, 6, 6, startAngle, endAngle)
                        ctx.closePath()
                        ctx.fill()
                    }
                }
            }
        }

        // ── Audio indicator (12x12 Lucide mic icon, visible when audioEnabled) ──
        SvgIcon {
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            visible: root.audioEnabled
            source: "qrc:/icons/icons/mic.svg"
            color: ComponentTokens.recordingAudioActive
        }

        // ── Duration label ──
        Text {
            id: durationLabel
            text: root.isPreparing
                ? (root.preparingStatus || qsTr("Preparing..."))
                : root.duration
            font.family: root.monoFont
            font.pixelSize: 11
            font.weight: Font.DemiBold
            color: root.isPreparing
                ? Qt.rgba(root.themeText.r, root.themeText.g,
                          root.themeText.b, 180/255)
                : root.themeText
            Layout.preferredWidth: Math.max(implicitWidth, 55)
        }

        // ── Separator 1 ──
        Text {
            text: "|"
            font.pixelSize: 11
            color: root.themeSeparator
        }

        // ── Size label ──
        Text {
            text: root.regionSize
            font.family: root.monoFont
            font.pixelSize: 10
            color: root.themeText
            Layout.preferredWidth: Math.max(implicitWidth, 70)
        }

        // ── Separator 2 ──
        Text {
            text: "|"
            font.pixelSize: 11
            color: root.themeSeparator
        }

        // ── FPS label ──
        Text {
            text: root.fpsText
            font.family: root.monoFont
            font.pixelSize: 10
            color: root.themeText
            Layout.preferredWidth: Math.max(implicitWidth, 45)
        }
    }

    // ========================================================================
    // Button area (right side: pause, stop, cancel)
    // ========================================================================
    Row {
        id: buttonArea
        anchors {
            right: parent.right
            rightMargin: root.contentMarginH
            verticalCenter: parent.verticalCenter
        }
        spacing: root.buttonSpacing
        z: 2

        // ── Pause/Resume button ──
        Item {
            width: root.buttonSize
            height: root.buttonSize
            opacity: root.isPreparing ? 0.5 : 1.0

            Rectangle {
                id: pauseHoverBg
                anchors.fill: parent
                anchors.margins: 2
                radius: 6
                color: root.themeHoverBg
                visible: pauseMouseArea.containsMouse && !root.isPreparing
            }

            SvgIcon {
                anchors.centerIn: parent
                source: root.isPaused
                    ? "qrc:/icons/icons/play.svg"
                    : "qrc:/icons/icons/pause.svg"
                iconSize: 16
                color: pauseMouseArea.containsMouse && !root.isPreparing
                    ? root.themeIconActive : root.themeIconNormal
            }

            MouseArea {
                id: pauseMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.ArrowCursor
                enabled: !root.isPreparing

                onClicked: {
                    if (root.isPaused)
                        root.resumeRequested()
                    else
                        root.pauseRequested()
                }

                onContainsMouseChanged: {
                    if (containsMouse) {
                        var mapped = parent.mapToItem(root, 0, 0)
                        root.buttonHovered(0, mapped.x, mapped.y,
                                           parent.width, parent.height)
                    } else {
                        root.buttonUnhovered()
                    }
                }
            }
        }

        // ── Stop button ──
        Item {
            width: root.buttonSize
            height: root.buttonSize
            opacity: root.isPreparing ? 0.5 : 1.0

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: 6
                color: root.themeHoverBg
                visible: stopMouseArea.containsMouse && !root.isPreparing
            }

            SvgIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/icons/stop.svg"
                iconSize: 16
                color: root.themeIconRecord
            }

            MouseArea {
                id: stopMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.ArrowCursor
                enabled: !root.isPreparing

                onClicked: root.stopRequested()

                onContainsMouseChanged: {
                    if (containsMouse) {
                        var mapped = parent.mapToItem(root, 0, 0)
                        root.buttonHovered(1, mapped.x, mapped.y,
                                           parent.width, parent.height)
                    } else {
                        root.buttonUnhovered()
                    }
                }
            }
        }

        // ── Cancel button ──
        Item {
            width: root.buttonSize
            height: root.buttonSize

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: 6
                color: root.themeHoverBg
                visible: cancelMouseArea.containsMouse
            }

            SvgIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/icons/cancel.svg"
                iconSize: 16
                color: cancelMouseArea.containsMouse
                    ? root.themeIconCancel : root.themeIconNormal
            }

            MouseArea {
                id: cancelMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.ArrowCursor

                onClicked: root.cancelRequested()

                onContainsMouseChanged: {
                    if (containsMouse) {
                        var mapped = parent.mapToItem(root, 0, 0)
                        root.buttonHovered(2, mapped.x, mapped.y,
                                           parent.width, parent.height)
                    } else {
                        root.buttonUnhovered()
                    }
                }
            }
        }
    }
}
