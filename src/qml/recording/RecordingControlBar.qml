import QtQuick
import QtQuick.Layouts
import SnapTrayQml

/**
 * RecordingControlBar: Floating control bar shown during screen recording.
 *
 * Features:
 *   - Glass-effect background (gradient + border + highlight)
 *   - Animated recording indicator (conical gradient / paused / preparing)
 *   - Duration label
 *   - Stop button with hover state
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

    // ── Theme colors from ComponentTokens (Overlay — always dark) ──
    readonly property color themeGlassBg: ComponentTokens.recordingControlBarBg
    readonly property color themeGlassBgTop: ComponentTokens.recordingControlBarBgTop
    readonly property color themeHighlight: ComponentTokens.recordingControlBarHighlight
    readonly property color themeBorder: ComponentTokens.recordingControlBarBorder
    readonly property color themeText: ComponentTokens.recordingControlBarText
    readonly property color themeSeparator: ComponentTokens.recordingControlBarSeparator
    readonly property color themeHoverBg: ComponentTokens.recordingControlBarHoverBg
    readonly property color themeIconNormal: ComponentTokens.recordingControlBarIconNormal
    readonly property color themeIconActive: ComponentTokens.recordingControlBarIconActive
    readonly property color themeIconRecord: ComponentTokens.recordingControlBarIconRecord
    readonly property color themeIconCancel: ComponentTokens.recordingControlBarIconCancel
    readonly property int themeCornerRadius: ComponentTokens.recordingControlBarRadius

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

    // ── Layout constants from ComponentTokens ──
    readonly property int barHeight: ComponentTokens.recordingControlBarHeight
    readonly property int buttonSize: ComponentTokens.recordingControlBarButtonSize
    readonly property int buttonSpacing: ComponentTokens.recordingControlBarButtonSpacing
    readonly property int contentMarginH: ComponentTokens.recordingControlBarMarginH
    readonly property int contentMarginV: ComponentTokens.recordingControlBarMarginV
    readonly property int itemSpacing: ComponentTokens.recordingControlBarItemSpacing

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

        // ── Recording indicator (animated circle) ──
        Canvas {
            id: indicatorCanvas
            Layout.preferredWidth: ComponentTokens.recordingControlBarIndicatorSize
            Layout.preferredHeight: ComponentTokens.recordingControlBarIndicatorSize
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
            // Pre-extract gradient RGB from tokens for Canvas rendering
            readonly property color gradColor0: ComponentTokens.recordingBoundaryGradientStart
            readonly property color gradColor1: ComponentTokens.recordingBoundaryGradientMid1
            readonly property color gradColor2: ComponentTokens.recordingBoundaryGradientMid2

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                var cx = width / 2
                var cy = height / 2
                var rad = Math.min(cx, cy)

                if (root.isPreparing) {
                    // Gray with sine pulse alpha: 128 ± 64
                    var alpha = (128 + 64 * Math.sin(gradientAngle * 2 * Math.PI)) / 255
                    ctx.fillStyle = Qt.rgba(180/255, 180/255, 180/255, alpha)
                    ctx.beginPath()
                    ctx.arc(cx, cy, rad, 0, 2 * Math.PI)
                    ctx.fill()
                } else if (root.isPaused) {
                    ctx.fillStyle = ComponentTokens.recordingBoundaryPaused
                    ctx.beginPath()
                    ctx.arc(cx, cy, rad, 0, 2 * Math.PI)
                    ctx.fill()
                } else {
                    // Conical gradient: blue → indigo → purple → blue
                    var c0r = gradColor0.r, c0g = gradColor0.g, c0b = gradColor0.b
                    var c1r = gradColor1.r, c1g = gradColor1.g, c1b = gradColor1.b
                    var c2r = gradColor2.r, c2g = gradColor2.g, c2b = gradColor2.b
                    var segments = 48
                    var angleOffset = gradientAngle * 2 * Math.PI
                    for (var i = 0; i < segments; i++) {
                        var t = i / segments
                        var r, g, b
                        if (t < 0.33) {
                            var p = t / 0.33
                            r = c0r + (c1r - c0r) * p
                            g = c0g + (c1g - c0g) * p
                            b = c0b + (c1b - c0b) * p
                        } else if (t < 0.66) {
                            var p2 = (t - 0.33) / 0.33
                            r = c1r + (c2r - c1r) * p2
                            g = c1g + (c2g - c1g) * p2
                            b = c1b + (c2b - c1b) * p2
                        } else {
                            var p3 = (t - 0.66) / 0.34
                            r = c2r + (c0r - c2r) * p3
                            g = c2g + (c0g - c2g) * p3
                            b = c2b + (c0b - c2b) * p3
                        }

                        var startAngle = angleOffset + (i / segments) * 2 * Math.PI
                        var endAngle = angleOffset + ((i + 1) / segments) * 2 * Math.PI

                        ctx.fillStyle = Qt.rgba(r, g, b, 1.0)
                        ctx.beginPath()
                        ctx.moveTo(cx, cy)
                        ctx.arc(cx, cy, rad, startAngle, endAngle)
                        ctx.closePath()
                        ctx.fill()
                    }
                }
            }
        }

        // ── Duration label ──
        Text {
            id: durationLabel
            text: root.isPreparing
                ? (root.preparingStatus || qsTr("Preparing..."))
                : root.duration
            font.family: root.monoFont
            font.pixelSize: ComponentTokens.recordingControlBarFontSize
            font.weight: Font.DemiBold
            color: root.isPreparing
                ? Qt.rgba(root.themeText.r, root.themeText.g,
                          root.themeText.b, 180/255)
                : root.themeText
            Layout.preferredWidth: Math.max(implicitWidth, 55)
        }

    }

    // ========================================================================
    // Button area (right side: stop)
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

        // ── Stop button ──
        Item {
            width: root.buttonSize
            height: root.buttonSize
            opacity: root.isPreparing ? 0.5 : 1.0

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: ComponentTokens.recordingControlBarButtonRadius
                color: root.themeHoverBg
                visible: stopMouseArea.containsMouse && !root.isPreparing
            }

            SvgIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/icons/stop.svg"
                iconSize: ComponentTokens.recordingControlBarIconSize
                color: root.themeIconRecord
            }

            MouseArea {
                id: stopMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: CursorTokens.toolbarControl
                enabled: !root.isPreparing

                onClicked: root.stopRequested()

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
    }
}
