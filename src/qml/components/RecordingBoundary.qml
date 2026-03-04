import QtQuick

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

    // Border mode: 0 = Recording, 1 = Playing, 2 = Paused
    property int borderMode: 0

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

    // Pre-computed gradient color stops (avoids recreating per frame)
    readonly property var gradientColors: [
        [0/255, 122/255, 255/255],    // Apple Blue #007AFF
        [88/255, 86/255, 214/255],     // Indigo #5856D6
        [175/255, 82/255, 222/255],    // Purple #AF52DE
        [191/255, 90/255, 242/255],    // Light Purple #BF5AF2
        [0/255, 122/255, 255/255]      // Back to Blue
    ]

    // ---- Animations ----

    NumberAnimation {
        id: gradientAnimation
        target: root
        property: "gradientAngle"
        from: 0.0
        to: 1.0
        duration: 3200   // full rotation in ~3.2s (matches 0.005 per 16ms)
        loops: Animation.Infinite
        running: root.borderMode === 0
    }

    NumberAnimation {
        id: pulseAnimation
        target: root
        property: "pulseOffset"
        from: 0.0
        to: 1.0
        duration: 3200   // full pulse cycle in ~3.2s (matches original offset rate)
        loops: Animation.Infinite
        running: root.borderMode === 1
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

            if (root.borderMode === 0) {
                drawRecordingBorder(ctx, bx, by, bw, bh);
            } else if (root.borderMode === 1) {
                drawPlayingBorder(ctx, bx, by, bw, bh);
            } else {
                drawPausedBorder(ctx, bx, by, bw, bh);
            }
        }

        function drawRecordingBorder(ctx, bx, by, bw, bh) {
            // Draw 3 outer glow layers (indigo with decreasing alpha)
            for (var i = 3; i >= 1; --i) {
                var alpha = Math.round(30 / i) / 255;
                ctx.strokeStyle = Qt.rgba(88/255, 86/255, 214/255, alpha);
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

                // Get perimeter points for this segment
                var p0 = perimeterPoint(t0, bx, by, bw, bh);
                var p1 = perimeterPoint(t1, bx, by, bw, bh);

                ctx.beginPath();
                ctx.moveTo(p0[0], p0[1]);
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
                ctx.strokeStyle = Qt.rgba(52/255, 199/255, 89/255, alpha);
                ctx.lineWidth = root.borderWidth + i * 2;
                ctx.lineJoin = "miter";
                ctx.beginPath();
                ctx.rect(bx - i, by - i, bw + 2*i, bh + 2*i);
                ctx.stroke();
            }

            // Main green border with pulsing alpha
            ctx.strokeStyle = Qt.rgba(52/255, 199/255, 89/255, pulseAlpha);
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
                ctx.strokeStyle = Qt.rgba(255/255, 184/255, 0/255, alpha);
                ctx.lineWidth = root.borderWidth + i * 2;
                ctx.lineJoin = "miter";
                ctx.beginPath();
                ctx.rect(bx - i, by - i, bw + 2*i, bh + 2*i);
                ctx.stroke();
            }

            // Static amber border
            ctx.strokeStyle = Qt.rgba(255/255, 184/255, 0, 1.0);
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
