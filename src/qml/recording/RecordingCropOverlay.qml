import QtQuick
import SnapTrayQml

/**
 * Crop editor drawn over the recording preview video.
 *
 * Works in item coordinates; the backend converts to (and normalizes) video pixels.
 * States: no crop (committedRect empty), editing a draft, committed crop.
 * While editing, a press inside the selection moves it, a press on an edge or
 * handle resizes it, and a drag elsewhere inside the video creates a new one.
 * Snapping only ever picks targets that keep the rect inside the content and
 * at least minViewSide wide/tall.
 */
Item {
    id: overlay

    property rect contentRect: Qt.rect(0, 0, 0, 0)
    property rect committedRect: Qt.rect(0, 0, 0, 0)
    property size videoSize: Qt.size(0, 0)
    property color accentColor: SemanticTokens.accentDefault
    property bool editing: false
    property rect draftRect: Qt.rect(0, 0, 0, 0)
    // False blocks pointer edits (for example while an export is processing).
    property bool interactive: true

    signal applyRequested(rect viewRect)
    signal cancelRequested()

    readonly property int edgeLeft: 1
    readonly property int edgeRight: 2
    readonly property int edgeTop: 4
    readonly property int edgeBottom: 8
    readonly property real handleSize: 10
    readonly property real handleRadius: 2
    readonly property real borderWidth: 2
    readonly property real snapDistance: 6
    readonly property real minViewSide: 8
    // Pointer travel before a press outside the selection starts a new one,
    // so a plain click does not replace the selection with a sliver.
    readonly property real createDragThreshold: 3
    readonly property real sizeChipHeight: 24
    readonly property rect shownRect: editing ? draftRect : committedRect
    // committedRect is empty when there is no crop (RecordingPreview passes Qt.rect(0, 0, 0, 0)).
    readonly property bool hasShownRect: isNonEmpty(shownRect)
    readonly property bool hasContent: contentRect.width > 0 && contentRect.height > 0
            && videoSize.width > 0 && videoSize.height > 0

    // The draft lives in view coordinates; a new content geometry would
    // reinterpret it, so drop it and keep the committed video-pixel crop.
    onContentRectChanged: cancel()
    onVideoSizeChanged: cancel()

    function isNonEmpty(r) { return r.width > 0 && r.height > 0 }
    function sameRect(a, b) {
        return a.x === b.x && a.y === b.y && a.width === b.width && a.height === b.height
    }

    function beginEditing() {
        if (editing || !hasContent)
            return
        draftRect = committedRect
        editing = true
    }
    function apply() {
        if (!editing)
            return
        editing = false
        // Copy: a value-type property read is a live reference, and draftRect is reset below.
        const draft = Qt.rect(draftRect.x, draftRect.y, draftRect.width, draftRect.height)
        draftRect = Qt.rect(0, 0, 0, 0)
        // An empty draft is a no-op; an untouched draft keeps the committed crop exactly.
        if (isNonEmpty(draft) && !sameRect(draft, committedRect))
            applyRequested(draft)
    }
    function cancel() {
        if (!editing)
            return
        editing = false
        draftRect = Qt.rect(0, 0, 0, 0)
        cancelRequested()
    }

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
    // Unbounded snap to the nearest target within snapDistance. Geometry helpers use
    // snapWithin so that only targets inside the legal range are eligible.
    function snapValue(v, targets) {
        return snapWithin(v, targets, -Infinity, Infinity)
    }
    // Clamp v to [lo, hi], then snap to the nearest target within snapDistance
    // that also lies in [lo, hi]. Targets outside the range are never eligible.
    function snapWithin(v, targets, lo, hi) {
        const bounded = clamp(v, lo, hi)
        let best = bounded
        let distance = snapDistance + 1
        for (let i = 0; i < targets.length; ++i) {
            const target = targets[i]
            const delta = Math.abs(bounded - target)
            if (target >= lo && target <= hi && delta <= snapDistance && delta < distance) {
                best = target
                distance = delta
            }
        }
        return best
    }
    function xTargets() { return [contentRect.x, contentRect.x + contentRect.width / 2, contentRect.x + contentRect.width] }
    function yTargets() { return [contentRect.y, contentRect.y + contentRect.height / 2, contentRect.y + contentRect.height] }

    function createRect(px, py, x, y) {
        const c = contentRect
        const right = c.x + c.width
        const bottom = c.y + c.height
        // Snap the near edge inside the content, then the far edge between it and the content edge.
        const x0 = snapWithin(Math.min(px, x), xTargets(), c.x, right)
        const y0 = snapWithin(Math.min(py, y), yTargets(), c.y, bottom)
        const x1 = snapWithin(Math.max(px, x), xTargets(), x0, right)
        const y1 = snapWithin(Math.max(py, y), yTargets(), y0, bottom)
        return Qt.rect(x0, y0, x1 - x0, y1 - y0)
    }
    function moveRect(r, dx, dy) {
        const c = contentRect
        // Either edge may align, but the rectangle must stay entirely inside content.
        const xs = xTargets().concat(xTargets().map(function(v) { return v - r.width }))
        const ys = yTargets().concat(yTargets().map(function(v) { return v - r.height }))
        return Qt.rect(snapWithin(r.x + dx, xs, c.x, Math.max(c.x, c.x + c.width - r.width)),
                       snapWithin(r.y + dy, ys, c.y, Math.max(c.y, c.y + c.height - r.height)),
                       r.width, r.height)
    }
    function resizeRect(r, edges, dx, dy) {
        const c = contentRect
        let left = r.x, top = r.y, right = r.x + r.width, bottom = r.y + r.height
        if (edges & edgeLeft)
            left = snapWithin(left + dx, xTargets(), c.x, Math.max(c.x, right - minViewSide))
        if (edges & edgeRight)
            right = snapWithin(right + dx, xTargets(), Math.min(c.x + c.width, left + minViewSide), c.x + c.width)
        if (edges & edgeTop)
            top = snapWithin(top + dy, yTargets(), c.y, Math.max(c.y, bottom - minViewSide))
        if (edges & edgeBottom)
            bottom = snapWithin(bottom + dy, yTargets(), Math.min(c.y + c.height, top + minViewSide), c.y + c.height)
        return Qt.rect(left, top, right - left, bottom - top)
    }
    function containsPoint(r, x, y) {
        return x > r.x && x < r.x + r.width && y > r.y && y < r.y + r.height
    }
    function edgesAt(x, y) {
        const r = draftRect
        if (!isNonEmpty(r))
            return 0
        const near = x >= r.x - handleSize && x <= r.x + r.width + handleSize
                && y >= r.y - handleSize && y <= r.y + r.height + handleSize
        if (!near)
            return 0
        let edges = 0
        const dl = Math.abs(x - r.x)
        const dr = Math.abs(x - (r.x + r.width))
        const dt = Math.abs(y - r.y)
        const db = Math.abs(y - (r.y + r.height))
        if (Math.min(dl, dr) <= handleSize)
            edges |= dl <= dr ? edgeLeft : edgeRight
        if (Math.min(dt, db) <= handleSize)
            edges |= dt <= db ? edgeTop : edgeBottom
        return edges
    }
    function cursorFor(x, y) {
        const e = edgesAt(x, y)
        if ((e & edgeLeft && e & edgeTop) || (e & edgeRight && e & edgeBottom)) return Qt.SizeFDiagCursor
        if ((e & edgeRight && e & edgeTop) || (e & edgeLeft && e & edgeBottom)) return Qt.SizeBDiagCursor
        if (e & (edgeLeft | edgeRight)) return Qt.SizeHorCursor
        if (e & (edgeTop | edgeBottom)) return Qt.SizeVerCursor
        if (containsPoint(draftRect, x, y)) return Qt.SizeAllCursor
        if (containsPoint(contentRect, x, y)) return CursorTokens.captureSelection
        return CursorTokens.defaultCursor
    }

    // Dim everything inside the video but outside the crop.
    Repeater {
        model: overlay.hasShownRect ? 4 : 0
        delegate: Rectangle {
            required property int index
            readonly property rect c: overlay.contentRect
            readonly property rect s: overlay.shownRect
            color: SemanticTokens.backgroundOverlay
            x: index === 2 ? s.x + s.width : c.x
            y: index === 0 ? c.y : index === 1 ? s.y + s.height : s.y
            width: index < 2 ? c.width : index === 2 ? c.x + c.width - (s.x + s.width) : s.x - c.x
            height: index === 0 ? s.y - c.y : index === 1 ? c.y + c.height - (s.y + s.height) : s.height
        }
    }

    Rectangle {
        objectName: "cropSelectionFrame"
        visible: overlay.hasShownRect
        x: overlay.shownRect.x
        y: overlay.shownRect.y
        width: overlay.shownRect.width
        height: overlay.shownRect.height
        color: "transparent"
        border.color: overlay.accentColor
        border.width: overlay.borderWidth
    }

    Repeater {
        objectName: "cropHandles"
        model: overlay.editing && overlay.hasShownRect
               ? [[0, 0], [0.5, 0], [1, 0], [0, 0.5], [1, 0.5], [0, 1], [0.5, 1], [1, 1]] : []
        delegate: Rectangle {
            required property var modelData
            width: overlay.handleSize
            height: overlay.handleSize
            radius: overlay.handleRadius
            color: overlay.accentColor
            x: overlay.draftRect.x + overlay.draftRect.width * modelData[0] - width / 2
            y: overlay.draftRect.y + overlay.draftRect.height * modelData[1] - height / 2
        }
    }

    GlassSurface {
        id: draftSizeChip
        objectName: "cropSizeLabel"
        visible: overlay.editing && overlay.hasShownRect && overlay.hasContent
        width: draftSizeText.implicitWidth + SemanticTokens.spacing16
        height: overlay.sizeChipHeight
        // Below the selection's bottom-right corner, kept inside the overlay.
        x: overlay.clamp(overlay.draftRect.x + overlay.draftRect.width - width,
                         0, Math.max(0, overlay.width - width))
        y: overlay.clamp(overlay.draftRect.y + overlay.draftRect.height + SemanticTokens.spacing4,
                         0, Math.max(0, overlay.height - height - SemanticTokens.spacing4))
        glassBg: ComponentTokens.tooltipBackground
        glassBgTop: ComponentTokens.tooltipBackgroundTop
        glassHighlight: ComponentTokens.tooltipHighlight
        glassBorder: ComponentTokens.tooltipBorder
        glassRadius: ComponentTokens.tooltipRadius

        Text {
            id: draftSizeText
            anchors.centerIn: parent
            // Approximate; the committed (normalized) size is shown by the toolbar chip.
            text: overlay.hasContent
                  ? Math.round(overlay.draftRect.width * overlay.videoSize.width / overlay.contentRect.width)
                    + " × " + Math.round(overlay.draftRect.height * overlay.videoSize.height / overlay.contentRect.height)
                  : ""
            color: SemanticTokens.textPrimary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
        }
    }

    MouseArea {
        id: cropMouse
        objectName: "cropMouseArea"
        anchors.fill: parent
        // MouseArea.enabled does not drop the area from the window's cursor lookup, so it is
        // hidden outside editing (the click-to-play hand underneath shows through) and its
        // cursor is reset whenever it is disabled (for example while processing).
        visible: overlay.editing
        enabled: overlay.editing && overlay.interactive
        hoverEnabled: enabled
        preventStealing: true
        cursorShape: enabled ? overlay.cursorFor(mouseX, mouseY) : undefined

        readonly property int modeNone: 0
        readonly property int modeCreate: 1
        readonly property int modeMove: 2
        readonly property int modeResize: 3

        property int mode: modeNone
        property bool createStarted: false
        property int edges: 0
        property real pressX: 0
        property real pressY: 0
        property rect pressRect: Qt.rect(0, 0, 0, 0)

        onPressed: function(mouse) {
            pressX = mouse.x
            pressY = mouse.y
            pressRect = overlay.draftRect
            createStarted = false
            edges = overlay.edgesAt(mouse.x, mouse.y)
            if (edges !== 0)
                mode = modeResize
            else if (overlay.containsPoint(overlay.draftRect, mouse.x, mouse.y))
                mode = modeMove
            else if (overlay.containsPoint(overlay.contentRect, mouse.x, mouse.y))
                mode = modeCreate
            else
                mode = modeNone
        }
        onPositionChanged: function(mouse) {
            if (!pressed || mode === modeNone)
                return
            const dx = mouse.x - pressX
            const dy = mouse.y - pressY
            if (mode === modeCreate) {
                if (!createStarted && Math.abs(dx) < overlay.createDragThreshold
                        && Math.abs(dy) < overlay.createDragThreshold)
                    return
                createStarted = true
                overlay.draftRect = overlay.createRect(pressX, pressY, mouse.x, mouse.y)
            } else if (mode === modeMove) {
                overlay.draftRect = overlay.moveRect(pressRect, dx, dy)
            } else {
                overlay.draftRect = overlay.resizeRect(pressRect, edges, dx, dy)
            }
        }
        onReleased: mode = modeNone
        onCanceled: {
            if (mode !== modeNone && overlay.editing)
                overlay.draftRect = pressRect
            mode = modeNone
        }
    }
}
