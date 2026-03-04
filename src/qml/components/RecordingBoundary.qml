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
    readonly property int glowPadding: 10  // space for 3 glow layers (2px each + border)

    // Total size = region + border + glow on each side
    width: regionWidth + 2 * (borderWidth + glowPadding)
    height: regionHeight + 2 * (borderWidth + glowPadding)

    // Animation angle for recording mode gradient rotation (0..1)
    property real gradientAngle: 0.0

    // Animation offset for playing mode pulse (0..1, wraps)
    property real pulseOffset: 0.0

    // Gradient endpoint colors (no primitive token for these specific hues)
    readonly property color colorAppleBlue: "#007AFF"
    readonly property color colorLightPurple: "#BF5AF2"

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
            // Draw 3 outer glow layers (indigo with decreasing alpha)
            for (var i = 3; i >= 1; --i) {
                var alpha = Math.round(30 / i) / 255;
                ctx.strokeStyle = Qt.rgba(PrimitiveTokens.indigo500.r, PrimitiveTokens.indigo500.g, PrimitiveTokens.indigo500.b, alpha);
                ctx.lineWidth = root.borderWidth + i * 2;
                ctx.lineJoin = "miter";
                ctx.beginPath();
                var gi = i;
                ctx.rect(bx - gi, by - gi, bw + 2*gi, bh + 2*gi);
                ctx.stroke();
            }

            // Draw conical gradient border using segmented approach
            // We approximate a conical gradient by drawing small segments
            // around the rectangle perimeter (48 segments = 12 per side)
            var segments = 48;
            var colors = root.gradientColors;

            ctx.lineWidth = root.borderWidth;
            ctx.lineJoin = "miter";
            ctx.lineCap = "butt";

            // Corner t-values (top-left is always t=0, so only 3 corners)
            var perimeter = 2 * (bw + bh);
            var cornerTs = [
                bw / perimeter,               // top-right
                (bw + bh) / perimeter,        // bottom-right
                (2 * bw + bh) / perimeter     // bottom-left
            ];
            var eps = 1e-6;

            for (var s = 0; s < segments; s++) {
                var t0 = s / segments;
                var t1 = (s + 1) / segments;

                // Map perimeter position to angle and apply offset
                var angle = (t0 + root.gradientAngle) % 1.0;

                // Interpolate color from gradient stops
                var colorIdx = angle * 4; // 4 segments between 5 stops
                var ci = Math.floor(colorIdx);
                var cf = colorIdx - ci;
                if (ci >= 4) { ci = 3; cf = 1.0; }
                var c0 = colors[ci];
                var c1 = colors[ci + 1];
                var r = c0[0] + (c1[0] - c0[0]) * cf;
                var g = c0[1] + (c1[1] - c0[1]) * cf;
                var b = c0[2] + (c1[2] - c0[2]) * cf;

                ctx.strokeStyle = Qt.rgba(r, g, b, 1.0);

                // Route through any corner that falls within this segment
                var p0 = perimeterPoint(t0, bx, by, bw, bh);
                ctx.beginPath();
                ctx.moveTo(p0[0], p0[1]);

                for (var c = 0; c < 3; c++) {
                    if (cornerTs[c] > t0 + eps && cornerTs[c] < t1 - eps) {
                        var cp = perimeterPoint(cornerTs[c], bx, by, bw, bh);
                        ctx.lineTo(cp[0], cp[1]);
                    }
                }

                var p1 = perimeterPoint(t1, bx, by, bw, bh);
                ctx.lineTo(p1[0], p1[1]);
                ctx.stroke();
            }
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

            // Draw 3 outer glow layers (green with pulsing + fading alpha)
            for (var i = 3; i >= 1; --i) {
                var alpha = (30 / i) / 255 * pulseAlpha;
                ctx.strokeStyle = Qt.rgba(PrimitiveTokens.green500.r, PrimitiveTokens.green500.g, PrimitiveTokens.green500.b, alpha);
                ctx.lineWidth = root.borderWidth + i * 2;
                ctx.lineJoin = "miter";
                ctx.beginPath();
                ctx.rect(bx - i, by - i, bw + 2*i, bh + 2*i);
                ctx.stroke();
            }

            // Main green border with pulsing alpha
            ctx.strokeStyle = Qt.rgba(PrimitiveTokens.green500.r, PrimitiveTokens.green500.g, PrimitiveTokens.green500.b, pulseAlpha);
            ctx.lineWidth = root.borderWidth;
            ctx.lineJoin = "miter";
            ctx.beginPath();
            ctx.rect(bx, by, bw, bh);
            ctx.stroke();
        }

        function drawPausedBorder(ctx, bx, by, bw, bh) {
            // Draw 3 outer glow layers (amber with fading alpha)
            for (var i = 3; i >= 1; --i) {
                var alpha = (30 / i) / 255;
                ctx.strokeStyle = Qt.rgba(PrimitiveTokens.amber600.r, PrimitiveTokens.amber600.g, PrimitiveTokens.amber600.b, alpha);
                ctx.lineWidth = root.borderWidth + i * 2;
                ctx.lineJoin = "miter";
                ctx.beginPath();
                ctx.rect(bx - i, by - i, bw + 2*i, bh + 2*i);
                ctx.stroke();
            }

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
