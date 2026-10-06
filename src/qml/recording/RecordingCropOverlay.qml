import QtQuick
import SnapTrayQml

/**
 * Crop editor drawn over the recording preview video.
 *
 * Works in item coordinates; the backend converts to (and normalizes) video pixels.
 * States: no crop (committedRect empty), editing a draft, committed crop.
 * While editing, a press inside the selection moves it, a press on an edge or
 * handle resizes it, and a drag elsewhere inside the video creates a new one.
 * Creating, resizing and snapping only ever pick geometry that keeps the rect
 * inside the content and at least the backend's minimum crop size (minVideoSide,
 * converted to view px) wide/tall, so applying a draft never visibly jumps.
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
    // The backend grows any committed crop below this many video px per side
    // (capped at the even-floored frame size). 0 falls back to minViewSide.
    property int minVideoSide: 0
    // Window under the pointer at the playhead, in view coordinates, as the
    // preview looks it up from the recording's window timeline. Empty = none.
    property rect hoverRect: Qt.rect(0, 0, 0, 0)
    property string hoverLabel: ""
    property point hoverPoint: Qt.point(0, 0)
    property bool hovering: false
    // The pointer moved or left while editing (not during a drag).
    signal hoverChanged()
    // A click inside an existing draft or on its handles moves/resizes it and never snaps,
    // so the highlight is only shown where a click would actually snap.
    readonly property bool showsHover: editing && hovering && isNonEmpty(hoverRect) && !cropMouse.pressed
            && !containsPoint(draftRect, hoverPoint.x, hoverPoint.y) && edgesAt(hoverPoint.x, hoverPoint.y) === 0
    readonly property bool pressed: cropMouse.pressed
    readonly property real hoverFillAlpha: 0.12
    // What a click would select: the hovered window clamped into the content.
    readonly property rect hoverShownRect: isNonEmpty(hoverRect) ? snapToRect(hoverRect) : Qt.rect(0, 0, 0, 0)
    // Pointer state is meaningless once editing ends; a stale highlight must not return.
    onEditingChanged: {
        if (editing)
            return
        hovering = false
        hoverRect = Qt.rect(0, 0, 0, 0)
        hoverLabel = ""
    }

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
    // Floor for drafts in view px, so a selection stays grabbable even when the
    // backend minimum maps to less (for example a 4K video in a small window).
    readonly property real minViewSide: 8
    // Smallest draft per axis in view px: minVideoSide at the current scale, at
    // least minViewSide, never wider or taller than the content itself.
    readonly property real minViewWidth: hasContent
            ? Math.min(contentRect.width, Math.max(minViewSide,
                  Math.min(minVideoSide, videoSize.width - videoSize.width % 2) * contentRect.width / videoSize.width))
            : minViewSide
    readonly property real minViewHeight: hasContent
            ? Math.min(contentRect.height, Math.max(minViewSide,
                  Math.min(minVideoSide, videoSize.height - videoSize.height % 2) * contentRect.height / videoSize.height))
            : minViewSide
    // Pointer travel before a press outside the selection starts a new one,
    // so a plain click does not replace the selection with a sliver.
    readonly property real createDragThreshold: 3
    readonly property real sizeChipHeight: 24
    readonly property rect shownRect: editing ? draftRect : committedRect
    // committedRect is empty when there is no crop (RecordingPreview passes Qt.rect(0, 0, 0, 0)).
    readonly property bool hasShownRect: isNonEmpty(shownRect)
    readonly property bool hasContent: contentRect.width > 0 && contentRect.height > 0
            && videoSize.width > 0 && videoSize.height > 0

    // The draft lives in view coordinates. When the content is re-fitted (the
    // window was resized) it follows the same video pixels, so editing goes on.
    // Without content to follow, or for a different video, it is dropped and
    // the committed video-pixel crop stays.
    property rect lastContentRect: Qt.rect(0, 0, 0, 0)
    Component.onCompleted: lastContentRect = contentRect
    onContentRectChanged: {
        const previous = Qt.rect(lastContentRect.x, lastContentRect.y, lastContentRect.width, lastContentRect.height)
        lastContentRect = contentRect
        if (!editing)
            return
        // Read the inputs, not hasContent: a derived property may not have
        // been re-evaluated yet when this handler runs.
        const haveVideo = videoSize.width > 0 && videoSize.height > 0
        if (!isNonEmpty(contentRect) || !haveVideo || !isNonEmpty(previous)) {
            cancel()
            return
        }
        draftRect = mapRect(draftRect, previous, contentRect)
    }
    onVideoSizeChanged: cancel()

    function isNonEmpty(r) { return r.width > 0 && r.height > 0 }
    function sameRect(a, b) {
        return a.x === b.x && a.y === b.y && a.width === b.width && a.height === b.height
    }
    // The same video pixels as r (in `from`), in a re-fitted content rect `to`.
    function mapRect(r, from, to) {
        const sx = to.width / from.width
        const sy = to.height / from.height
        return Qt.rect(to.x + (r.x - from.x) * sx, to.y + (r.y - from.y) * sy, r.width * sx, r.height * sy)
    }

    // r clamped into the content and grown around its centre to the minimum
    // draft size, so a tiny window still gives a usable selection.
    function snapToRect(r) {
        const c = contentRect
        let left = clamp(r.x, c.x, c.x + c.width)
        let right = clamp(r.x + r.width, c.x, c.x + c.width)
        let top = clamp(r.y, c.y, c.y + c.height)
        let bottom = clamp(r.y + r.height, c.y, c.y + c.height)
        if (right - left < minViewWidth) {
            left = clamp((left + right - minViewWidth) / 2, c.x, c.x + c.width - minViewWidth)
            right = left + minViewWidth
        }
        if (bottom - top < minViewHeight) {
            top = clamp((top + bottom - minViewHeight) / 2, c.y, c.y + c.height - minViewHeight)
            bottom = top + minViewHeight
        }
        return Qt.rect(left, top, right - left, bottom - top)
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

    // New selection from the press point (px, py) to the pointer (x, y).
    function createRect(px, py, x, y) {
        const c = contentRect
        const h = createSpan(px, x, xTargets(), c.x, c.x + c.width, minViewWidth)
        const v = createSpan(py, y, yTargets(), c.y, c.y + c.height, minViewHeight)
        return Qt.rect(h.lo, v.lo, h.hi - h.lo, v.hi - v.lo)
    }
    // One axis of a new selection. The press point stays the anchor (snapped), the
    // pointer side keeps at least minSide away from it, and both stay in [lo, hi].
    // Only when the minimum would not fit does the anchor retreat into the content.
    function createSpan(anchor, pointer, targets, lo, hi, minSide) {
        const a = snapWithin(anchor, targets, lo, hi)
        const side = Math.min(minSide, hi - lo)
        if (pointer >= anchor) {
            const start = Math.min(a, hi - side)
            return { lo: start, hi: snapWithin(pointer, targets, start + side, hi) }
        }
        const end = Math.max(a, lo + side)
        return { lo: snapWithin(pointer, targets, lo, end - side), hi: end }
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
        // The dragged edge stops at the minimum size; the opposite edge never moves.
        if (edges & edgeLeft)
            left = snapWithin(left + dx, xTargets(), c.x, Math.max(c.x, right - minViewWidth))
        if (edges & edgeRight)
            right = snapWithin(right + dx, xTargets(), Math.min(c.x + c.width, left + minViewWidth), c.x + c.width)
        if (edges & edgeTop)
            top = snapWithin(top + dy, yTargets(), c.y, Math.max(c.y, bottom - minViewHeight))
        if (edges & edgeBottom)
            bottom = snapWithin(bottom + dy, yTargets(), Math.min(c.y + c.height, top + minViewHeight), c.y + c.height)
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
        // Inside the rect the edge bands shrink with its size so its centre always
        // moves it; outside, the full handleSize band keeps edges and corners easy
        // to grab. Each axis is judged on its own, so a point beside a small rect
        // hits that one edge rather than a corner.
        const insideX = x > r.x && x < r.x + r.width
        const insideY = y > r.y && y < r.y + r.height
        const toleranceX = insideX ? Math.min(handleSize, r.width / 4) : handleSize
        const toleranceY = insideY ? Math.min(handleSize, r.height / 4) : handleSize
        let edges = 0
        const dl = Math.abs(x - r.x)
        const dr = Math.abs(x - (r.x + r.width))
        const dt = Math.abs(y - r.y)
        const db = Math.abs(y - (r.y + r.height))
        if (Math.min(dl, dr) <= toleranceX)
            edges |= dl <= dr ? edgeLeft : edgeRight
        if (Math.min(dt, db) <= toleranceY)
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
            // Approximate; the committed (normalized) size is shown by the crop
            // size chip in the top-left corner of the video.
            text: overlay.hasContent
                  ? Math.round(overlay.draftRect.width * overlay.videoSize.width / overlay.contentRect.width)
                    + " × " + Math.round(overlay.draftRect.height * overlay.videoSize.height / overlay.contentRect.height)
                  : ""
            color: SemanticTokens.textPrimary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
        }
    }

    Rectangle {
        objectName: "cropHoverFrame"
        visible: overlay.showsHover
        x: overlay.hoverShownRect.x
        y: overlay.hoverShownRect.y
        width: overlay.hoverShownRect.width
        height: overlay.hoverShownRect.height
        color: Qt.rgba(overlay.accentColor.r, overlay.accentColor.g, overlay.accentColor.b, overlay.hoverFillAlpha)
        border.color: overlay.accentColor
        border.width: overlay.borderWidth
    }

    GlassSurface {
        objectName: "cropHoverLabel"
        visible: overlay.showsHover && overlay.hoverLabel.length > 0
        width: hoverLabelText.implicitWidth + SemanticTokens.spacing16
        height: overlay.sizeChipHeight
        x: overlay.clamp(overlay.hoverShownRect.x + SemanticTokens.spacing4, 0, Math.max(0, overlay.width - width))
        y: overlay.clamp(overlay.hoverShownRect.y + SemanticTokens.spacing4, 0, Math.max(0, overlay.height - height))
        glassBg: ComponentTokens.tooltipBackground
        glassBgTop: ComponentTokens.tooltipBackgroundTop
        glassHighlight: ComponentTokens.tooltipHighlight
        glassBorder: ComponentTokens.tooltipBorder
        glassRadius: ComponentTokens.tooltipRadius

        Text {
            id: hoverLabelText
            anchors.centerIn: parent
            text: overlay.hoverLabel
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
        // The pointer left the click radius at some point during this press.
        property bool moved: false
        property int edges: 0
        property real pressX: 0
        property real pressY: 0
        property rect pressRect: Qt.rect(0, 0, 0, 0)

        onPressed: function(mouse) {
            pressX = mouse.x
            pressY = mouse.y
            pressRect = overlay.draftRect
            createStarted = false
            moved = false
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
        function updateHover(x, y) {
            if (!enabled || pressed)
                return
            overlay.hoverPoint = Qt.point(x, y)
            overlay.hovering = true
            overlay.hoverChanged()
        }
        // Entering (or revealing the area under a stationary pointer) need not
        // produce positionChanged. Initialize hover for that event too.
        onEntered: updateHover(mouseX, mouseY)
        onPositionChanged: function(mouse) {
            if (!pressed) {
                updateHover(mouse.x, mouse.y)
                return
            }
            updateDraft(mouse.x, mouse.y)
        }
        function updateDraft(x, y) {
            if (mode === modeNone)
                return
            const dx = x - pressX
            const dy = y - pressY
            if (dx === 0 && dy === 0 && !moved)
                return
            if (Math.abs(dx) >= overlay.createDragThreshold || Math.abs(dy) >= overlay.createDragThreshold)
                moved = true
            if (mode === modeCreate) {
                if (!createStarted && Math.abs(dx) < overlay.createDragThreshold
                        && Math.abs(dy) < overlay.createDragThreshold)
                    return
                createStarted = true
                overlay.draftRect = overlay.createRect(pressX, pressY, x, y)
            } else if (mode === modeMove) {
                overlay.draftRect = overlay.moveRect(pressRect, dx, dy)
            } else {
                overlay.draftRect = overlay.resizeRect(pressRect, edges, dx, dy)
            }
        }

        onExited: {
            overlay.hovering = false
            overlay.hoverChanged()
        }
        onReleased: function(mouse) {
            // Native moves may be coalesced or queued. Commit the release position
            // against the press snapshot before ending the gesture.
            updateDraft(mouse.x, mouse.y)
            // Only a click that started outside any draft snaps; inside one it is a no-op.
            if (!moved && mode === modeCreate && overlay.isNonEmpty(overlay.hoverRect))
                overlay.draftRect = overlay.hoverShownRect
            mode = modeNone
            // The pointer may have left the window it hovered before the press:
            // re-evaluate at the release point instead of showing a stale highlight.
            overlay.hoverPoint = Qt.point(mouse.x, mouse.y)
            overlay.hoverChanged()
        }
        onCanceled: {
            if (mode !== modeNone && overlay.editing)
                overlay.draftRect = pressRect
            mode = modeNone
        }
    }
}
