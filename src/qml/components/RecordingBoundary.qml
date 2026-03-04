import QtQuick
import SnapTrayQml

/**
 * RecordingBoundary.qml
 *
 * GPU-accelerated recording boundary overlay with three visual modes:
 *   - Recording: rotating conical gradient border (blue/indigo/purple)
 *   - Playing:   pulsing green border
 *   - Paused:    static amber border
 *
 * Designed to be loaded in a frameless, transparent, click-through QQuickView.
 * The C++ bridge sets `borderMode`, `regionWidth`, and `regionHeight`.
 */
Item {
    id: root

    // Border mode constants (mirrors C++ QmlRecordingBoundary::BorderMode)
    readonly property int modeRecording: 0
    readonly property int modePlaying: 1
    readonly property int modePaused: 2

    property int borderMode: modeRecording

    // Region dimensions (set from C++)
    property int regionWidth: 400
    property int regionHeight: 300

    // Constants
    readonly property int borderWidth: 4
    readonly property int cornerRadius: 10
    readonly property int glowPadding: 10

    // Total size = region + border + glow on each side
    width: regionWidth + 2 * (borderWidth + glowPadding)
    height: regionHeight + 2 * (borderWidth + glowPadding)

    // Animation angle for recording mode gradient rotation (0..1)
    property real gradientAngle: 0.0

    // Animation offset for playing mode pulse (0..1, wraps)
    property real pulseOffset: 0.0

    // Gradient endpoint colors
    readonly property color colorAppleBlue: PrimitiveTokens.boundaryBlue
    readonly property color colorLightPurple: PrimitiveTokens.boundaryPurple

    // Extract [r, g, b] from a QML color for Canvas use
    function colorToRgb(c) { return [c.r, c.g, c.b]; }

    // Pre-computed gradient color stops (avoids recreating per frame)
    readonly property var gradientColors: [
        colorToRgb(colorAppleBlue),
        colorToRgb(PrimitiveTokens.indigo500),
        colorToRgb(PrimitiveTokens.purple400),
        colorToRgb(colorLightPurple),
        colorToRgb(colorAppleBlue)
    ]

    // ---- Animations ----

    NumberAnimation {
        id: gradientAnimation
        target: root
        property: "gradientAngle"
        from: 0.0
        to: 1.0
        duration: PrimitiveTokens.durationBoundaryLoop
        loops: Animation.Infinite
        running: root.borderMode === root.modeRecording
    }

    NumberAnimation {
        id: pulseAnimation
        target: root
        property: "pulseOffset"
        from: 0.0
        to: 1.0
        duration: PrimitiveTokens.durationBoundaryLoop
        loops: Animation.Infinite
        running: root.borderMode === root.modePlaying
    }

    // ---- Canvas for all drawing ----

    Canvas {
        id: borderCanvas
        anchors.fill: parent
        renderStrategy: Canvas.Threaded

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);

            // Border rect (inset by glow padding + half border width)
            var bx = root.glowPadding + root.borderWidth / 2;
            var by = root.glowPadding + root.borderWidth / 2;
            var bw = width - 2 * bx;
            var bh = height - 2 * by;

            if (root.borderMode === root.modeRecording) {
                drawRecordingBorder(ctx, bx, by, bw, bh);
            } else if (root.borderMode === root.modePlaying) {
                drawPlayingBorder(ctx, bx, by, bw, bh);
            } else {
                drawPausedBorder(ctx, bx, by, bw, bh);
            }
        }

        function drawRecordingBorder(ctx, bx, by, bw, bh) {
            var segments = 48;
            var colors = root.gradientColors;
            var hw = root.borderWidth / 2;

            // Draw full rectangle with miter joins for perfect corners
            ctx.lineWidth = root.borderWidth;
            ctx.lineJoin = "miter";
            ctx.strokeStyle = "white";
            ctx.beginPath();
            ctx.rect(bx, by, bw, bh);
            ctx.stroke();

            // Paint gradient colors onto the stroke using source-atop compositing
            ctx.globalCompositeOperation = "source-atop";

            for (var s = 0; s < segments; s++) {
                var t0 = s / segments;
                var t1 = (s + 1) / segments;

                var angle = (t0 + root.gradientAngle) % 1.0;

                var colorIdx = angle * 4;
                var ci = Math.floor(colorIdx);
                var cf = colorIdx - ci;
                if (ci >= 4) { ci = 3; cf = 1.0; }
                var c0 = colors[ci];
                var c1 = colors[ci + 1];
                var r = c0[0] + (c1[0] - c0[0]) * cf;
                var g = c0[1] + (c1[1] - c0[1]) * cf;
                var b = c0[2] + (c1[2] - c0[2]) * cf;

                ctx.fillStyle = Qt.rgba(r, g, b, 1.0);

                var p0 = perimeterPoint(t0, bx, by, bw, bh);
                var p1 = perimeterPoint(t1, bx, by, bw, bh);

                var x0 = Math.min(p0[0], p1[0]) - hw;
                var y0 = Math.min(p0[1], p1[1]) - hw;
                var x1 = Math.max(p0[0], p1[0]) + hw;
                var y1 = Math.max(p0[1], p1[1]) + hw;

                ctx.fillRect(x0, y0, x1 - x0, y1 - y0);
            }

            ctx.globalCompositeOperation = "source-over";
        }

        function perimeterPoint(t, bx, by, bw, bh) {
            // Map t (0..1) to a point on the rectangle perimeter
            // Starting from top-left, going clockwise: top, right, bottom, left
            var perimeter = 2 * (bw + bh);
            var dist = t * perimeter;

            if (dist <= bw) {
                // Top edge
                return [bx + dist, by];
            }
            dist -= bw;
            if (dist <= bh) {
                // Right edge
                return [bx + bw, by + dist];
            }
            dist -= bh;
            if (dist <= bw) {
                // Bottom edge
                return [bx + bw - dist, by + bh];
            }
            dist -= bw;
            // Left edge
            return [bx, by + bh - dist];
        }

        function drawPlayingBorder(ctx, bx, by, bw, bh) {
            // Pulsing alpha using sine wave
            var pulseAlpha = 0.6 + 0.4 * Math.sin(root.pulseOffset * 2 * Math.PI);

            // Main green border with pulsing alpha
            ctx.strokeStyle = Qt.rgba(PrimitiveTokens.green500.r, PrimitiveTokens.green500.g, PrimitiveTokens.green500.b, pulseAlpha);
            ctx.lineWidth = root.borderWidth;
            ctx.lineJoin = "miter";
            ctx.beginPath();
            ctx.rect(bx, by, bw, bh);
            ctx.stroke();
        }

        function drawPausedBorder(ctx, bx, by, bw, bh) {
            // Static amber border
            ctx.strokeStyle = PrimitiveTokens.amber600;
            ctx.lineWidth = root.borderWidth;
            ctx.lineJoin = "miter";
            ctx.beginPath();
            ctx.rect(bx, by, bw, bh);
            ctx.stroke();
        }
    }

    // Repaint when animated properties change
    onGradientAngleChanged: borderCanvas.requestPaint()
    onPulseOffsetChanged: borderCanvas.requestPaint()
    onBorderModeChanged: {
        // Reset animation values on mode change
        gradientAngle = 0.0;
        pulseOffset = 0.0;
        borderCanvas.requestPaint();
    }
}
